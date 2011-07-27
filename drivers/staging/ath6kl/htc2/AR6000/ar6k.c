//------------------------------------------------------------------------------
// <copyright file="ar6k.c" company="Atheros">
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

#include "a_config.h"
#include "athdefs.h"
#include "hw/mbox_host_reg.h"
#include "a_osapi.h"
#include "../htc_debug.h"
#include "hif.h"
#include "htc_packet.h"
#include "ar6k.h"

#define MAILBOX_FOR_BLOCK_SIZE          1

int DevEnableInterrupts(struct ar6k_device *pDev);
int DevDisableInterrupts(struct ar6k_device *pDev);

static void DevCleanupVirtualScatterSupport(struct ar6k_device *pDev);

void AR6KFreeIOPacket(struct ar6k_device *pDev, struct htc_packet *pPacket)
{
    LOCK_AR6K(pDev);
    HTC_PACKET_ENQUEUE(&pDev->RegisterIOList,pPacket);
    UNLOCK_AR6K(pDev);
}

struct htc_packet *AR6KAllocIOPacket(struct ar6k_device *pDev)
{
    struct htc_packet *pPacket;

    LOCK_AR6K(pDev);
    pPacket = HTC_PACKET_DEQUEUE(&pDev->RegisterIOList);
    UNLOCK_AR6K(pDev);

    return pPacket;
}

void DevCleanup(struct ar6k_device *pDev)
{
    DevCleanupGMbox(pDev);

    if (pDev->HifAttached) {
        HIFDetachHTC(pDev->HIFDevice);
        pDev->HifAttached = false;
    }

    DevCleanupVirtualScatterSupport(pDev);

    if (A_IS_MUTEX_VALID(&pDev->Lock)) {
        A_MUTEX_DELETE(&pDev->Lock);
    }
}

int DevSetup(struct ar6k_device *pDev)
{
    u32 blocksizes[AR6K_MAILBOXES];
    int status = 0;
    int      i;
    HTC_CALLBACKS htcCallbacks;

    do {

        DL_LIST_INIT(&pDev->ScatterReqHead);
           /* initialize our free list of IO packets */
        INIT_HTC_PACKET_QUEUE(&pDev->RegisterIOList);
        A_MUTEX_INIT(&pDev->Lock);

        A_MEMZERO(&htcCallbacks, sizeof(HTC_CALLBACKS));
            /* the device layer handles these */
        htcCallbacks.rwCompletionHandler = DevRWCompletionHandler;
        htcCallbacks.dsrHandler = DevDsrHandler;
        htcCallbacks.context = pDev;

        status = HIFAttachHTC(pDev->HIFDevice, &htcCallbacks);

        if (status) {
            break;
        }

        pDev->HifAttached = true;

            /* get the addresses for all 4 mailboxes */
        status = HIFConfigureDevice(pDev->HIFDevice, HIF_DEVICE_GET_MBOX_ADDR,
                                    &pDev->MailBoxInfo, sizeof(pDev->MailBoxInfo));

        if (status) {
            A_ASSERT(false);
            break;
        }

            /* carve up register I/O packets (these are for ASYNC register I/O ) */
        for (i = 0; i < AR6K_MAX_REG_IO_BUFFERS; i++) {
            struct htc_packet *pIOPacket;
            pIOPacket = &pDev->RegIOBuffers[i].HtcPacket;
            SET_HTC_PACKET_INFO_RX_REFILL(pIOPacket,
                                          pDev,
                                          pDev->RegIOBuffers[i].Buffer,
                                          AR6K_REG_IO_BUFFER_SIZE,
                                          0); /* don't care */
            AR6KFreeIOPacket(pDev,pIOPacket);
        }

            /* get the block sizes */
        status = HIFConfigureDevice(pDev->HIFDevice, HIF_DEVICE_GET_MBOX_BLOCK_SIZE,
                                    blocksizes, sizeof(blocksizes));

        if (status) {
            A_ASSERT(false);
            break;
        }

            /* note: we actually get the block size of a mailbox other than 0, for SDIO the block
             * size on mailbox 0 is artificially set to 1.  So we use the block size that is set
             * for the other 3 mailboxes */
        pDev->BlockSize = blocksizes[MAILBOX_FOR_BLOCK_SIZE];
            /* must be a power of 2 */
        A_ASSERT((pDev->BlockSize & (pDev->BlockSize - 1)) == 0);

            /* assemble mask, used for padding to a block */
        pDev->BlockMask = pDev->BlockSize - 1;

        AR_DEBUG_PRINTF(ATH_DEBUG_TRC,("BlockSize: %d, MailboxAddress:0x%X \n",
                    pDev->BlockSize, pDev->MailBoxInfo.MboxAddresses[HTC_MAILBOX]));

        pDev->GetPendingEventsFunc = NULL;
            /* see if the HIF layer implements the get pending events function  */
        HIFConfigureDevice(pDev->HIFDevice,
                           HIF_DEVICE_GET_PENDING_EVENTS_FUNC,
                           &pDev->GetPendingEventsFunc,
                           sizeof(pDev->GetPendingEventsFunc));

            /* assume we can process HIF interrupt events asynchronously */
        pDev->HifIRQProcessingMode = HIF_DEVICE_IRQ_ASYNC_SYNC;

            /* see if the HIF layer overrides this assumption */
        HIFConfigureDevice(pDev->HIFDevice,
                           HIF_DEVICE_GET_IRQ_PROC_MODE,
                           &pDev->HifIRQProcessingMode,
                           sizeof(pDev->HifIRQProcessingMode));

        switch (pDev->HifIRQProcessingMode) {
            case HIF_DEVICE_IRQ_SYNC_ONLY:
                AR_DEBUG_PRINTF(ATH_DEBUG_WARN,("HIF Interrupt processing is SYNC ONLY\n"));
                    /* see if HIF layer wants HTC to yield */
                HIFConfigureDevice(pDev->HIFDevice,
                                   HIF_DEVICE_GET_IRQ_YIELD_PARAMS,
                                   &pDev->HifIRQYieldParams,
                                   sizeof(pDev->HifIRQYieldParams));

                if (pDev->HifIRQYieldParams.RecvPacketYieldCount > 0) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_WARN,
                        ("HIF requests that DSR yield per %d RECV packets \n",
                        pDev->HifIRQYieldParams.RecvPacketYieldCount));
                    pDev->DSRCanYield = true;
                }
                break;
            case HIF_DEVICE_IRQ_ASYNC_SYNC:
                AR_DEBUG_PRINTF(ATH_DEBUG_TRC,("HIF Interrupt processing is ASYNC and SYNC\n"));
                break;
            default:
                A_ASSERT(false);
        }

        pDev->HifMaskUmaskRecvEvent = NULL;

            /* see if the HIF layer implements the mask/unmask recv events function  */
        HIFConfigureDevice(pDev->HIFDevice,
                           HIF_DEVICE_GET_RECV_EVENT_MASK_UNMASK_FUNC,
                           &pDev->HifMaskUmaskRecvEvent,
                           sizeof(pDev->HifMaskUmaskRecvEvent));

        AR_DEBUG_PRINTF(ATH_DEBUG_TRC,("HIF special overrides : 0x%lX , 0x%lX\n",
                 (unsigned long)pDev->GetPendingEventsFunc, (unsigned long)pDev->HifMaskUmaskRecvEvent));

        status = DevDisableInterrupts(pDev);

        if (status) {
            break;
        }

        status = DevSetupGMbox(pDev);

    } while (false);

    if (status) {
        if (pDev->HifAttached) {
            HIFDetachHTC(pDev->HIFDevice);
            pDev->HifAttached = false;
        }
    }

    return status;

}

int DevEnableInterrupts(struct ar6k_device *pDev)
{
    int                  status;
    struct ar6k_irq_enable_registers regs;

    LOCK_AR6K(pDev);

        /* Enable all the interrupts except for the internal AR6000 CPU interrupt */
    pDev->IrqEnableRegisters.int_status_enable = INT_STATUS_ENABLE_ERROR_SET(0x01) |
                                      INT_STATUS_ENABLE_CPU_SET(0x01) |
                                      INT_STATUS_ENABLE_COUNTER_SET(0x01);

    if (NULL == pDev->GetPendingEventsFunc) {
        pDev->IrqEnableRegisters.int_status_enable |= INT_STATUS_ENABLE_MBOX_DATA_SET(0x01);
    } else {
        /* The HIF layer provided us with a pending events function which means that
         * the detection of pending mbox messages is handled in the HIF layer.
         * This is the case for the SPI2 interface.
         * In the normal case we enable MBOX interrupts, for the case
         * with HIFs that offer this mechanism, we keep these interrupts
         * masked */
        pDev->IrqEnableRegisters.int_status_enable &= ~INT_STATUS_ENABLE_MBOX_DATA_SET(0x01);
    }


    /* Set up the CPU Interrupt Status Register */
    pDev->IrqEnableRegisters.cpu_int_status_enable = CPU_INT_STATUS_ENABLE_BIT_SET(0x00);

    /* Set up the Error Interrupt Status Register */
    pDev->IrqEnableRegisters.error_status_enable =
                                  ERROR_STATUS_ENABLE_RX_UNDERFLOW_SET(0x01) |
                                  ERROR_STATUS_ENABLE_TX_OVERFLOW_SET(0x01);

    /* Set up the Counter Interrupt Status Register (only for debug interrupt to catch fatal errors) */
    pDev->IrqEnableRegisters.counter_int_status_enable =
        COUNTER_INT_STATUS_ENABLE_BIT_SET(AR6K_TARGET_DEBUG_INTR_MASK);

        /* copy into our temp area */
    memcpy(&regs,&pDev->IrqEnableRegisters,AR6K_IRQ_ENABLE_REGS_SIZE);

    UNLOCK_AR6K(pDev);

        /* always synchronous */
    status = HIFReadWrite(pDev->HIFDevice,
                          INT_STATUS_ENABLE_ADDRESS,
                          &regs.int_status_enable,
                          AR6K_IRQ_ENABLE_REGS_SIZE,
                          HIF_WR_SYNC_BYTE_INC,
                          NULL);

    if (status) {
        /* Can't write it for some reason */
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                        ("Failed to update interrupt control registers err: %d\n", status));

    }

    return status;
}

int DevDisableInterrupts(struct ar6k_device *pDev)
{
    struct ar6k_irq_enable_registers regs;

    LOCK_AR6K(pDev);
        /* Disable all interrupts */
    pDev->IrqEnableRegisters.int_status_enable = 0;
    pDev->IrqEnableRegisters.cpu_int_status_enable = 0;
    pDev->IrqEnableRegisters.error_status_enable = 0;
    pDev->IrqEnableRegisters.counter_int_status_enable = 0;
        /* copy into our temp area */
    memcpy(&regs,&pDev->IrqEnableRegisters,AR6K_IRQ_ENABLE_REGS_SIZE);

    UNLOCK_AR6K(pDev);

        /* always synchronous */
    return HIFReadWrite(pDev->HIFDevice,
                        INT_STATUS_ENABLE_ADDRESS,
                        &regs.int_status_enable,
                        AR6K_IRQ_ENABLE_REGS_SIZE,
                        HIF_WR_SYNC_BYTE_INC,
                        NULL);
}

/* enable device interrupts */
int DevUnmaskInterrupts(struct ar6k_device *pDev)
{
    /* for good measure, make sure interrupt are disabled before unmasking at the HIF
     * layer.
     * The rationale here is that between device insertion (where we clear the interrupts the first time)
     * and when HTC is finally ready to handle interrupts, other software can perform target "soft" resets.
     * The AR6K interrupt enables reset back to an "enabled" state when this happens.
     *  */
    int IntStatus = 0;
    DevDisableInterrupts(pDev);

#ifdef THREAD_X
    // Tobe verified...
    IntStatus = DevEnableInterrupts(pDev);
    /* Unmask the host controller interrupts */
    HIFUnMaskInterrupt(pDev->HIFDevice);
#else
    /* Unmask the host controller interrupts */
    HIFUnMaskInterrupt(pDev->HIFDevice);
    IntStatus = DevEnableInterrupts(pDev);
#endif

    return IntStatus;
}

/* disable all device interrupts */
int DevMaskInterrupts(struct ar6k_device *pDev)
{
        /* mask the interrupt at the HIF layer, we don't want a stray interrupt taken while
         * we zero out our shadow registers in DevDisableInterrupts()*/
    HIFMaskInterrupt(pDev->HIFDevice);

    return DevDisableInterrupts(pDev);
}

/* callback when our fetch to enable/disable completes */
static void DevDoEnableDisableRecvAsyncHandler(void *Context, struct htc_packet *pPacket)
{
    struct ar6k_device *pDev = (struct ar6k_device *)Context;

    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("+DevDoEnableDisableRecvAsyncHandler: (dev: 0x%lX)\n", (unsigned long)pDev));

    if (pPacket->Status) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                (" Failed to disable receiver, status:%d \n", pPacket->Status));
    }
        /* free this IO packet */
    AR6KFreeIOPacket(pDev,pPacket);
    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("-DevDoEnableDisableRecvAsyncHandler \n"));
}

/* disable packet reception (used in case the host runs out of buffers)
 * this is the "override" method when the HIF reports another methods to
 * disable recv events */
static int DevDoEnableDisableRecvOverride(struct ar6k_device *pDev, bool EnableRecv, bool AsyncMode)
{
    int                  status = 0;
    struct htc_packet                *pIOPacket = NULL;

    AR_DEBUG_PRINTF(ATH_DEBUG_TRC,("DevDoEnableDisableRecvOverride: Enable:%d Mode:%d\n",
            EnableRecv,AsyncMode));

    do {

        if (AsyncMode) {

            pIOPacket = AR6KAllocIOPacket(pDev);

            if (NULL == pIOPacket) {
                status = A_NO_MEMORY;
                A_ASSERT(false);
                break;
            }

                /* stick in our completion routine when the I/O operation completes */
            pIOPacket->Completion = DevDoEnableDisableRecvAsyncHandler;
            pIOPacket->pContext = pDev;

                /* call the HIF layer override and do this asynchronously */
            status = pDev->HifMaskUmaskRecvEvent(pDev->HIFDevice,
                                                 EnableRecv ? HIF_UNMASK_RECV : HIF_MASK_RECV,
                                                 pIOPacket);
            break;
        }

            /* if we get here we are doing it synchronously */
        status = pDev->HifMaskUmaskRecvEvent(pDev->HIFDevice,
                                             EnableRecv ? HIF_UNMASK_RECV : HIF_MASK_RECV,
                                             NULL);

    } while (false);

    if (status && (pIOPacket != NULL)) {
        AR6KFreeIOPacket(pDev,pIOPacket);
    }

    return status;
}

/* disable packet reception (used in case the host runs out of buffers)
 * this is the "normal" method using the interrupt enable registers through
 * the host I/F */
static int DevDoEnableDisableRecvNormal(struct ar6k_device *pDev, bool EnableRecv, bool AsyncMode)
{
    int                  status = 0;
    struct htc_packet                *pIOPacket = NULL;
    struct ar6k_irq_enable_registers regs;

        /* take the lock to protect interrupt enable shadows */
    LOCK_AR6K(pDev);

    if (EnableRecv) {
        pDev->IrqEnableRegisters.int_status_enable |= INT_STATUS_ENABLE_MBOX_DATA_SET(0x01);
    } else {
        pDev->IrqEnableRegisters.int_status_enable &= ~INT_STATUS_ENABLE_MBOX_DATA_SET(0x01);
    }

        /* copy into our temp area */
    memcpy(&regs,&pDev->IrqEnableRegisters,AR6K_IRQ_ENABLE_REGS_SIZE);
    UNLOCK_AR6K(pDev);

    do {

        if (AsyncMode) {

            pIOPacket = AR6KAllocIOPacket(pDev);

            if (NULL == pIOPacket) {
                status = A_NO_MEMORY;
                A_ASSERT(false);
                break;
            }

                /* copy values to write to our async I/O buffer */
            memcpy(pIOPacket->pBuffer,&regs,AR6K_IRQ_ENABLE_REGS_SIZE);

                /* stick in our completion routine when the I/O operation completes */
            pIOPacket->Completion = DevDoEnableDisableRecvAsyncHandler;
            pIOPacket->pContext = pDev;

                /* write it out asynchronously */
            HIFReadWrite(pDev->HIFDevice,
                         INT_STATUS_ENABLE_ADDRESS,
                         pIOPacket->pBuffer,
                         AR6K_IRQ_ENABLE_REGS_SIZE,
                         HIF_WR_ASYNC_BYTE_INC,
                         pIOPacket);
            break;
        }

        /* if we get here we are doing it synchronously */

        status = HIFReadWrite(pDev->HIFDevice,
                              INT_STATUS_ENABLE_ADDRESS,
                              &regs.int_status_enable,
                              AR6K_IRQ_ENABLE_REGS_SIZE,
                              HIF_WR_SYNC_BYTE_INC,
                              NULL);

    } while (false);

    if (status && (pIOPacket != NULL)) {
        AR6KFreeIOPacket(pDev,pIOPacket);
    }

    return status;
}


int DevStopRecv(struct ar6k_device *pDev, bool AsyncMode)
{
    if (NULL == pDev->HifMaskUmaskRecvEvent) {
        return DevDoEnableDisableRecvNormal(pDev,false,AsyncMode);
    } else {
        return DevDoEnableDisableRecvOverride(pDev,false,AsyncMode);
    }
}

int DevEnableRecv(struct ar6k_device *pDev, bool AsyncMode)
{
    if (NULL == pDev->HifMaskUmaskRecvEvent) {
        return DevDoEnableDisableRecvNormal(pDev,true,AsyncMode);
    } else {
        return DevDoEnableDisableRecvOverride(pDev,true,AsyncMode);
    }
}

int DevWaitForPendingRecv(struct ar6k_device *pDev,u32 TimeoutInMs,bool *pbIsRecvPending)
{
    int    status          = 0;
    u8     host_int_status = 0x0;
    u32 counter         = 0x0;

    if(TimeoutInMs < 100)
    {
        TimeoutInMs = 100;
    }

    counter = TimeoutInMs / 100;

    do
    {
        //Read the Host Interrupt Status Register
        status = HIFReadWrite(pDev->HIFDevice,
                              HOST_INT_STATUS_ADDRESS,
                             &host_int_status,
                              sizeof(u8),
                              HIF_RD_SYNC_BYTE_INC,
                              NULL);
        if (status)
        {
            AR_DEBUG_PRINTF(ATH_LOG_ERR,("DevWaitForPendingRecv:Read HOST_INT_STATUS_ADDRESS Failed 0x%X\n",status));
            break;
        }

        host_int_status = !status ? (host_int_status & (1 << 0)):0;
        if(!host_int_status)
        {
            status          = 0;
           *pbIsRecvPending = false;
            break;
        }
        else
        {
            *pbIsRecvPending = true;
        }

        A_MDELAY(100);

        counter--;

    }while(counter);
    return status;
}

void DevDumpRegisters(struct ar6k_device               *pDev,
                      struct ar6k_irq_proc_registers   *pIrqProcRegs,
                      struct ar6k_irq_enable_registers *pIrqEnableRegs)
{

    AR_DEBUG_PRINTF(ATH_DEBUG_ANY, ("\n<------- Register Table -------->\n"));

    if (pIrqProcRegs != NULL) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
            ("Host Int Status:           0x%x\n",pIrqProcRegs->host_int_status));
        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
            ("CPU Int Status:            0x%x\n",pIrqProcRegs->cpu_int_status));
        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
            ("Error Int Status:          0x%x\n",pIrqProcRegs->error_int_status));
        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
            ("Counter Int Status:        0x%x\n",pIrqProcRegs->counter_int_status));
        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
            ("Mbox Frame:                0x%x\n",pIrqProcRegs->mbox_frame));
        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
            ("Rx Lookahead Valid:        0x%x\n",pIrqProcRegs->rx_lookahead_valid));
        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
            ("Rx Lookahead 0:            0x%x\n",pIrqProcRegs->rx_lookahead[0]));
        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
            ("Rx Lookahead 1:            0x%x\n",pIrqProcRegs->rx_lookahead[1]));

        if (pDev->MailBoxInfo.GMboxAddress != 0) {
                /* if the target supports GMBOX hardware, dump some additional state */
            AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
                ("GMBOX Host Int Status 2:   0x%x\n",pIrqProcRegs->host_int_status2));
            AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
                ("GMBOX RX Avail:            0x%x\n",pIrqProcRegs->gmbox_rx_avail));
            AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
                ("GMBOX lookahead alias 0:   0x%x\n",pIrqProcRegs->rx_gmbox_lookahead_alias[0]));
            AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
                ("GMBOX lookahead alias 1:   0x%x\n",pIrqProcRegs->rx_gmbox_lookahead_alias[1]));
        }

    }

    if (pIrqEnableRegs != NULL) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
            ("Int Status Enable:         0x%x\n",pIrqEnableRegs->int_status_enable));
        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
            ("Counter Int Status Enable: 0x%x\n",pIrqEnableRegs->counter_int_status_enable));
    }
    AR_DEBUG_PRINTF(ATH_DEBUG_ANY, ("<------------------------------->\n"));
}


#define DEV_GET_VIRT_DMA_INFO(p)  ((struct dev_scatter_dma_virtual_info *)((p)->HIFPrivate[0]))

static struct hif_scatter_req *DevAllocScatterReq(struct hif_device *Context)
{
    struct dl_list *pItem;
    struct ar6k_device *pDev = (struct ar6k_device *)Context;
    LOCK_AR6K(pDev);
    pItem = DL_ListRemoveItemFromHead(&pDev->ScatterReqHead);
    UNLOCK_AR6K(pDev);
    if (pItem != NULL) {
        return A_CONTAINING_STRUCT(pItem, struct hif_scatter_req, ListLink);
    }
    return NULL;
}

static void DevFreeScatterReq(struct hif_device *Context, struct hif_scatter_req *pReq)
{
    struct ar6k_device *pDev = (struct ar6k_device *)Context;
    LOCK_AR6K(pDev);
    DL_ListInsertTail(&pDev->ScatterReqHead, &pReq->ListLink);
    UNLOCK_AR6K(pDev);
}

int DevCopyScatterListToFromDMABuffer(struct hif_scatter_req *pReq, bool FromDMA)
{
    u8 *pDMABuffer = NULL;
    int             i, remaining;
    u32 length;

    pDMABuffer = pReq->pScatterBounceBuffer;

    if (pDMABuffer == NULL) {
        A_ASSERT(false);
        return A_EINVAL;
    }

    remaining = (int)pReq->TotalLength;

    for (i = 0; i < pReq->ValidScatterEntries; i++) {

        length = min((int)pReq->ScatterList[i].Length, remaining);

        if (length != (int)pReq->ScatterList[i].Length) {
            A_ASSERT(false);
                /* there is a problem with the scatter list */
            return A_EINVAL;
        }

        if (FromDMA) {
                /* from DMA buffer */
            memcpy(pReq->ScatterList[i].pBuffer, pDMABuffer , length);
        } else {
                /* to DMA buffer */
            memcpy(pDMABuffer, pReq->ScatterList[i].pBuffer, length);
        }

        pDMABuffer += length;
        remaining -= length;
    }

    return 0;
}

static void DevReadWriteScatterAsyncHandler(void *Context, struct htc_packet *pPacket)
{
    struct ar6k_device     *pDev = (struct ar6k_device *)Context;
    struct hif_scatter_req *pReq = (struct hif_scatter_req *)pPacket->pPktContext;
    
    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,("+DevReadWriteScatterAsyncHandler: (dev: 0x%lX)\n", (unsigned long)pDev));
    
    pReq->CompletionStatus = pPacket->Status;

    AR6KFreeIOPacket(pDev,pPacket);

    pReq->CompletionRoutine(pReq);

    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,("-DevReadWriteScatterAsyncHandler \n"));
}

static int DevReadWriteScatter(struct hif_device *Context, struct hif_scatter_req *pReq)
{
    struct ar6k_device     *pDev = (struct ar6k_device *)Context;
    int        status = 0;
    struct htc_packet      *pIOPacket = NULL;
    u32 request = pReq->Request;

    do {

        if (pReq->TotalLength > AR6K_MAX_TRANSFER_SIZE_PER_SCATTER) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                            ("Invalid length: %d \n", pReq->TotalLength));
            break;
        }

        if (pReq->TotalLength == 0) {
            A_ASSERT(false);
            break;
        }

        if (request & HIF_ASYNCHRONOUS) {
                /* use an I/O packet to carry this request */
            pIOPacket = AR6KAllocIOPacket(pDev);
            if (NULL == pIOPacket) {
                status = A_NO_MEMORY;
                break;
            }

                /* save the request */
            pIOPacket->pPktContext = pReq;
                /* stick in our completion routine when the I/O operation completes */
            pIOPacket->Completion = DevReadWriteScatterAsyncHandler;
            pIOPacket->pContext = pDev;
        }

        if (request & HIF_WRITE) {
            /* in virtual DMA, we are issuing the requests through the legacy HIFReadWrite API
             * this API will adjust the address automatically for the last byte to fall on the mailbox
             * EOM. */

            /* if the address is an extended address, we can adjust the address here since the extended
             * address will bypass the normal checks in legacy HIF layers */
            if (pReq->Address == pDev->MailBoxInfo.MboxProp[HTC_MAILBOX].ExtendedAddress) {
                pReq->Address += pDev->MailBoxInfo.MboxProp[HTC_MAILBOX].ExtendedSize - pReq->TotalLength;
            }
        }

            /* use legacy readwrite */
        status = HIFReadWrite(pDev->HIFDevice,
                              pReq->Address,
                              DEV_GET_VIRT_DMA_INFO(pReq)->pVirtDmaBuffer,
                              pReq->TotalLength,
                              request,
                              (request & HIF_ASYNCHRONOUS) ? pIOPacket : NULL);

    } while (false);

    if ((status != A_PENDING) && status && (request & HIF_ASYNCHRONOUS)) {
        if (pIOPacket != NULL) {
            AR6KFreeIOPacket(pDev,pIOPacket);
        }
        pReq->CompletionStatus = status;
        pReq->CompletionRoutine(pReq);
        status = 0;
    }

    return status;
}


static void DevCleanupVirtualScatterSupport(struct ar6k_device *pDev)
{
    struct hif_scatter_req *pReq;

    while (1) {
        pReq = DevAllocScatterReq((struct hif_device *)pDev);
        if (NULL == pReq) {
            break;
        }
        kfree(pReq);
    }

}

    /* function to set up virtual scatter support if HIF layer has not implemented the interface */
static int DevSetupVirtualScatterSupport(struct ar6k_device *pDev)
{
    int                     status = 0;
    int                          bufferSize, sgreqSize;
    int                          i;
    struct dev_scatter_dma_virtual_info *pVirtualInfo;
    struct hif_scatter_req              *pReq;

    bufferSize = sizeof(struct dev_scatter_dma_virtual_info) +
                2 * (A_GET_CACHE_LINE_BYTES()) + AR6K_MAX_TRANSFER_SIZE_PER_SCATTER;

    sgreqSize = sizeof(struct hif_scatter_req) +
                    (AR6K_SCATTER_ENTRIES_PER_REQ - 1) * (sizeof(struct hif_scatter_item));

    for (i = 0; i < AR6K_SCATTER_REQS; i++) {
            /* allocate the scatter request, buffer info and the actual virtual buffer itself */
        pReq = (struct hif_scatter_req *)A_MALLOC(sgreqSize + bufferSize);

        if (NULL == pReq) {
            status = A_NO_MEMORY;
            break;
        }

        A_MEMZERO(pReq, sgreqSize);

            /* the virtual DMA starts after the scatter request struct */
        pVirtualInfo = (struct dev_scatter_dma_virtual_info *)((u8 *)pReq + sgreqSize);
        A_MEMZERO(pVirtualInfo, sizeof(struct dev_scatter_dma_virtual_info));

        pVirtualInfo->pVirtDmaBuffer = &pVirtualInfo->DataArea[0];
            /* align buffer to cache line in case host controller can actually DMA this */
        pVirtualInfo->pVirtDmaBuffer = A_ALIGN_TO_CACHE_LINE(pVirtualInfo->pVirtDmaBuffer);
            /* store the structure in the private area */
        pReq->HIFPrivate[0] = pVirtualInfo;
            /* we emulate a DMA bounce interface */
        pReq->ScatterMethod = HIF_SCATTER_DMA_BOUNCE;
        pReq->pScatterBounceBuffer = pVirtualInfo->pVirtDmaBuffer;
            /* free request to the list */
        DevFreeScatterReq((struct hif_device *)pDev,pReq);
    }

    if (status) {
        DevCleanupVirtualScatterSupport(pDev);
    } else {
        pDev->HifScatterInfo.pAllocateReqFunc = DevAllocScatterReq;
        pDev->HifScatterInfo.pFreeReqFunc = DevFreeScatterReq;
        pDev->HifScatterInfo.pReadWriteScatterFunc = DevReadWriteScatter;
        if (pDev->MailBoxInfo.MboxBusIFType == MBOX_BUS_IF_SPI) {
            AR_DEBUG_PRINTF(ATH_DEBUG_WARN, ("AR6K: SPI bus requires RX scatter limits\n"));
            pDev->HifScatterInfo.MaxScatterEntries = AR6K_MIN_SCATTER_ENTRIES_PER_REQ;
            pDev->HifScatterInfo.MaxTransferSizePerScatterReq = AR6K_MIN_TRANSFER_SIZE_PER_SCATTER;
        } else {
            pDev->HifScatterInfo.MaxScatterEntries = AR6K_SCATTER_ENTRIES_PER_REQ;
            pDev->HifScatterInfo.MaxTransferSizePerScatterReq = AR6K_MAX_TRANSFER_SIZE_PER_SCATTER;
        }
        pDev->ScatterIsVirtual = true;
    }

    return status;
}

int DevCleanupMsgBundling(struct ar6k_device *pDev)
{
    if(NULL != pDev)
    {
        DevCleanupVirtualScatterSupport(pDev);
    }

    return 0;
}

int DevSetupMsgBundling(struct ar6k_device *pDev, int MaxMsgsPerTransfer)
{
    int status;

    if (pDev->MailBoxInfo.Flags & HIF_MBOX_FLAG_NO_BUNDLING) {
        AR_DEBUG_PRINTF(ATH_DEBUG_WARN, ("HIF requires bundling disabled\n"));
        return A_ENOTSUP;
    }

    status = HIFConfigureDevice(pDev->HIFDevice,
                                HIF_CONFIGURE_QUERY_SCATTER_REQUEST_SUPPORT,
                                &pDev->HifScatterInfo,
                                sizeof(pDev->HifScatterInfo));

    if (status) {
        AR_DEBUG_PRINTF(ATH_DEBUG_WARN,
            ("AR6K: ** HIF layer does not support scatter requests (%d) \n",status));

            /* we can try to use a virtual DMA scatter mechanism using legacy HIFReadWrite() */
        status = DevSetupVirtualScatterSupport(pDev);

        if (!status) {
             AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
                ("AR6K: virtual scatter transfers enabled (max scatter items:%d: maxlen:%d) \n",
                    DEV_GET_MAX_MSG_PER_BUNDLE(pDev), DEV_GET_MAX_BUNDLE_LENGTH(pDev)));
        }

    } else {
        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
            ("AR6K: HIF layer supports scatter requests (max scatter items:%d: maxlen:%d) \n",
                    DEV_GET_MAX_MSG_PER_BUNDLE(pDev), DEV_GET_MAX_BUNDLE_LENGTH(pDev)));
    }

    if (!status) {
            /* for the recv path, the maximum number of bytes per recv bundle is just limited
             * by the maximum transfer size at the HIF layer */
        pDev->MaxRecvBundleSize = pDev->HifScatterInfo.MaxTransferSizePerScatterReq;

        if (pDev->MailBoxInfo.MboxBusIFType == MBOX_BUS_IF_SPI) {
            AR_DEBUG_PRINTF(ATH_DEBUG_WARN, ("AR6K : SPI bus requires TX bundling disabled\n"));
            pDev->MaxSendBundleSize = 0;
        } else {
                /* for the send path, the max transfer size is limited by the existence and size of
                 * the extended mailbox address range */
            if (pDev->MailBoxInfo.MboxProp[0].ExtendedAddress != 0) {
                pDev->MaxSendBundleSize = pDev->MailBoxInfo.MboxProp[0].ExtendedSize;
            } else {
                    /* legacy */
                pDev->MaxSendBundleSize = AR6K_LEGACY_MAX_WRITE_LENGTH;
            }

            if (pDev->MaxSendBundleSize > pDev->HifScatterInfo.MaxTransferSizePerScatterReq) {
                    /* limit send bundle size to what the HIF can support for scatter requests */
                pDev->MaxSendBundleSize = pDev->HifScatterInfo.MaxTransferSizePerScatterReq;
            }
        }

        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
            ("AR6K: max recv: %d max send: %d \n",
                    DEV_GET_MAX_BUNDLE_RECV_LENGTH(pDev), DEV_GET_MAX_BUNDLE_SEND_LENGTH(pDev)));

    }
    return status;
}

int DevSubmitScatterRequest(struct ar6k_device *pDev, struct hif_scatter_req *pScatterReq, bool Read, bool Async)
{
    int status;

    if (Read) {
            /* read operation */
        pScatterReq->Request = (Async) ? HIF_RD_ASYNC_BLOCK_FIX : HIF_RD_SYNC_BLOCK_FIX;
        pScatterReq->Address = pDev->MailBoxInfo.MboxAddresses[HTC_MAILBOX];
        A_ASSERT(pScatterReq->TotalLength <= (u32)DEV_GET_MAX_BUNDLE_RECV_LENGTH(pDev));
    } else {
        u32 mailboxWidth;

            /* write operation */
        pScatterReq->Request = (Async) ? HIF_WR_ASYNC_BLOCK_INC : HIF_WR_SYNC_BLOCK_INC;
        A_ASSERT(pScatterReq->TotalLength <= (u32)DEV_GET_MAX_BUNDLE_SEND_LENGTH(pDev));
        if (pScatterReq->TotalLength > AR6K_LEGACY_MAX_WRITE_LENGTH) {
                /* for large writes use the extended address */
            pScatterReq->Address = pDev->MailBoxInfo.MboxProp[HTC_MAILBOX].ExtendedAddress;
            mailboxWidth = pDev->MailBoxInfo.MboxProp[HTC_MAILBOX].ExtendedSize;
        } else {
            pScatterReq->Address = pDev->MailBoxInfo.MboxAddresses[HTC_MAILBOX];
            mailboxWidth = AR6K_LEGACY_MAX_WRITE_LENGTH;
        }

        if (!pDev->ScatterIsVirtual) {
            /* we are passing this scatter list down to the HIF layer' scatter request handler, fixup the address
             * so that the last byte falls on the EOM, we do this for those HIFs that support the
             * scatter API */
            pScatterReq->Address += (mailboxWidth - pScatterReq->TotalLength);
        }

    }

    AR_DEBUG_PRINTF(ATH_DEBUG_RECV | ATH_DEBUG_SEND,
                ("DevSubmitScatterRequest, Entries: %d, Total Length: %d Mbox:0x%X (mode: %s : %s)\n",
                pScatterReq->ValidScatterEntries,
                pScatterReq->TotalLength,
                pScatterReq->Address,
                Async ? "ASYNC" : "SYNC",
                (Read) ? "RD" : "WR"));

    status = DEV_PREPARE_SCATTER_OPERATION(pScatterReq);

    if (status) {
        if (Async) {
            pScatterReq->CompletionStatus = status;
            pScatterReq->CompletionRoutine(pScatterReq);
            return 0;
        }
        return status;
    }

    status = pDev->HifScatterInfo.pReadWriteScatterFunc(pDev->ScatterIsVirtual ? pDev : pDev->HIFDevice,
                                                        pScatterReq);
    if (!Async) {
            /* in sync mode, we can touch the scatter request */
        pScatterReq->CompletionStatus = status;
        DEV_FINISH_SCATTER_OPERATION(pScatterReq);
    } else {
        if (status == A_PENDING) {
            status = 0;
        }
    }

    return status;
}


#ifdef MBOXHW_UNIT_TEST


/* This is a mailbox hardware unit test that must be called in a schedulable context
 * This test is very simple, it will send a list of buffers with a counting pattern
 * and the target will invert the data and send the message back
 *
 * the unit test has the following constraints:
 *
 * The target has at least 8 buffers of 256 bytes each. The host will send
 * the following pattern of buffers in rapid succession :
 *
 * 1 buffer - 128 bytes
 * 1 buffer - 256 bytes
 * 1 buffer - 512 bytes
 * 1 buffer - 1024 bytes
 *
 * The host will send the buffers to one mailbox and wait for buffers to be reflected
 * back from the same mailbox. The target sends the buffers FIFO order.
 * Once the final buffer has been received for a mailbox, the next mailbox is tested.
 *
 *
 * Note:  To simplifythe test , we assume that the chosen buffer sizes
 *        will fall on a nice block pad
 *
 * It is expected that higher-order tests will be written to stress the mailboxes using
 * a message-based protocol (with some performance timming) that can create more
 * randomness in the packets sent over mailboxes.
 *
 * */

#define A_ROUND_UP_PWR2(x, align)    (((int) (x) + ((align)-1)) & ~((align)-1))

#define BUFFER_BLOCK_PAD 128

#if 0
#define BUFFER1 128
#define BUFFER2 256
#define BUFFER3 512
#define BUFFER4 1024
#endif

#if 1
#define BUFFER1 80
#define BUFFER2 200
#define BUFFER3 444
#define BUFFER4 800
#endif

#define TOTAL_BYTES (A_ROUND_UP_PWR2(BUFFER1,BUFFER_BLOCK_PAD) + \
                     A_ROUND_UP_PWR2(BUFFER2,BUFFER_BLOCK_PAD) + \
                     A_ROUND_UP_PWR2(BUFFER3,BUFFER_BLOCK_PAD) + \
                     A_ROUND_UP_PWR2(BUFFER4,BUFFER_BLOCK_PAD) )

#define TEST_BYTES (BUFFER1 +  BUFFER2 + BUFFER3 + BUFFER4)

#define TEST_CREDITS_RECV_TIMEOUT 100

static u8 g_Buffer[TOTAL_BYTES];
static u32 g_MailboxAddrs[AR6K_MAILBOXES];
static u32 g_BlockSizes[AR6K_MAILBOXES];

#define BUFFER_PROC_LIST_DEPTH 4

struct buffer_proc_list {
    u8 *pBuffer;
    u32 length;
};


#define PUSH_BUFF_PROC_ENTRY(pList,len,pCurrpos) \
{                                                   \
    (pList)->pBuffer = (pCurrpos);                  \
    (pList)->length = (len);                        \
    (pCurrpos) += (len);                            \
    (pList)++;                                      \
}

/* a simple and crude way to send different "message" sizes */
static void AssembleBufferList(struct buffer_proc_list *pList)
{
    u8 *pBuffer = g_Buffer;

#if BUFFER_PROC_LIST_DEPTH < 4
#error "Buffer processing list depth is not deep enough!!"
#endif

    PUSH_BUFF_PROC_ENTRY(pList,BUFFER1,pBuffer);
    PUSH_BUFF_PROC_ENTRY(pList,BUFFER2,pBuffer);
    PUSH_BUFF_PROC_ENTRY(pList,BUFFER3,pBuffer);
    PUSH_BUFF_PROC_ENTRY(pList,BUFFER4,pBuffer);

}

#define FILL_ZERO     true
#define FILL_COUNTING false
static void InitBuffers(bool Zero)
{
    u16 *pBuffer16 = (u16 *)g_Buffer;
    int      i;

        /* fill buffer with 16 bit counting pattern or zeros */
    for (i = 0; i <  (TOTAL_BYTES / 2) ; i++) {
        if (!Zero) {
            pBuffer16[i] = (u16)i;
        } else {
            pBuffer16[i] = 0;
        }
    }
}


static bool CheckOneBuffer(u16 *pBuffer16, int Length)
{
    int      i;
    u16 startCount;
    bool   success = true;

        /* get the starting count */
    startCount = pBuffer16[0];
        /* invert it, this is the expected value */
    startCount = ~startCount;
        /* scan the buffer and verify */
    for (i = 0; i < (Length / 2) ; i++,startCount++) {
            /* target will invert all the data */
        if ((u16)pBuffer16[i] != (u16)~startCount) {
            success = false;
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Invalid Data Got:0x%X, Expecting:0x%X (offset:%d, total:%d) \n",
                        pBuffer16[i], ((u16)~startCount), i, Length));
             AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("0x%X 0x%X 0x%X 0x%X \n",
                        pBuffer16[i], pBuffer16[i + 1], pBuffer16[i + 2],pBuffer16[i+3]));
            break;
        }
    }

    return success;
}

static bool CheckBuffers(void)
{
    int      i;
    bool   success = true;
    struct buffer_proc_list checkList[BUFFER_PROC_LIST_DEPTH];

        /* assemble the list */
    AssembleBufferList(checkList);

        /* scan the buffers and verify */
    for (i = 0; i < BUFFER_PROC_LIST_DEPTH ; i++) {
        success = CheckOneBuffer((u16 *)checkList[i].pBuffer, checkList[i].length);
        if (!success) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Buffer : 0x%X, Length:%d failed verify \n",
                        (u32)checkList[i].pBuffer, checkList[i].length));
            break;
        }
    }

    return success;
}

    /* find the end marker for the last buffer we will be sending */
static u16 GetEndMarker(void)
{
    u8 *pBuffer;
    struct buffer_proc_list checkList[BUFFER_PROC_LIST_DEPTH];

        /* fill up buffers with the normal counting pattern */
    InitBuffers(FILL_COUNTING);

        /* assemble the list we will be sending down */
    AssembleBufferList(checkList);
        /* point to the last 2 bytes of the last buffer */
    pBuffer = &(checkList[BUFFER_PROC_LIST_DEPTH - 1].pBuffer[(checkList[BUFFER_PROC_LIST_DEPTH - 1].length) - 2]);

        /* the last count in the last buffer is the marker */
    return (u16)pBuffer[0] | ((u16)pBuffer[1] << 8);
}

#define ATH_PRINT_OUT_ZONE ATH_DEBUG_ERR

/* send the ordered buffers to the target */
static int SendBuffers(struct ar6k_device *pDev, int mbox)
{
    int         status = 0;
    u32 request = HIF_WR_SYNC_BLOCK_INC;
    struct buffer_proc_list sendList[BUFFER_PROC_LIST_DEPTH];
    int              i;
    int              totalBytes = 0;
    int              paddedLength;
    int              totalwPadding = 0;

    AR_DEBUG_PRINTF(ATH_PRINT_OUT_ZONE, ("Sending buffers on mailbox : %d \n",mbox));

        /* fill buffer with counting pattern */
    InitBuffers(FILL_COUNTING);

        /* assemble the order in which we send */
    AssembleBufferList(sendList);

    for (i = 0; i < BUFFER_PROC_LIST_DEPTH; i++) {

            /* we are doing block transfers, so we need to pad everything to a block size */
        paddedLength = (sendList[i].length + (g_BlockSizes[mbox] - 1)) &
                       (~(g_BlockSizes[mbox] - 1));

            /* send each buffer synchronously */
        status = HIFReadWrite(pDev->HIFDevice,
                              g_MailboxAddrs[mbox],
                              sendList[i].pBuffer,
                              paddedLength,
                              request,
                              NULL);
        if (status) {
            break;
        }
        totalBytes += sendList[i].length;
        totalwPadding += paddedLength;
    }

    AR_DEBUG_PRINTF(ATH_PRINT_OUT_ZONE, ("Sent %d bytes (%d padded bytes) to mailbox : %d \n",totalBytes,totalwPadding,mbox));

    return status;
}

/* poll the mailbox credit counter until we get a credit or timeout */
static int GetCredits(struct ar6k_device *pDev, int mbox, int *pCredits)
{
    int status = 0;
    int      timeout = TEST_CREDITS_RECV_TIMEOUT;
    u8 credits = 0;
    u32 address;

    while (true) {

            /* Read the counter register to get credits, this auto-decrements  */
        address = COUNT_DEC_ADDRESS + (AR6K_MAILBOXES + mbox) * 4;
        status = HIFReadWrite(pDev->HIFDevice, address, &credits, sizeof(credits),
                              HIF_RD_SYNC_BYTE_FIX, NULL);
        if (status) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                ("Unable to decrement the command credit count register (mbox=%d)\n",mbox));
            status = A_ERROR;
            break;
        }

        if (credits) {
            break;
        }

        timeout--;

        if (timeout <= 0) {
              AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                (" Timeout reading credit registers (mbox=%d, address:0x%X) \n",mbox,address));
            status = A_ERROR;
            break;
        }

         /* delay a little, target may not be ready */
         A_MDELAY(1000);

    }

    if (status == 0) {
        *pCredits = credits;
    }

    return status;
}


/* wait for the buffers to come back */
static int RecvBuffers(struct ar6k_device *pDev, int mbox)
{
    int         status = 0;
    u32 request = HIF_RD_SYNC_BLOCK_INC;
    struct buffer_proc_list recvList[BUFFER_PROC_LIST_DEPTH];
    int              curBuffer;
    int              credits;
    int              i;
    int              totalBytes = 0;
    int              paddedLength;
    int              totalwPadding = 0;

    AR_DEBUG_PRINTF(ATH_PRINT_OUT_ZONE, ("Waiting for buffers on mailbox : %d \n",mbox));

        /* zero the buffers */
    InitBuffers(FILL_ZERO);

        /* assemble the order in which we should receive */
    AssembleBufferList(recvList);

    curBuffer = 0;

    while (curBuffer < BUFFER_PROC_LIST_DEPTH) {

            /* get number of buffers that have been completed, this blocks
             * until we get at least 1 credit or it times out */
        status = GetCredits(pDev, mbox, &credits);

        if (status) {
            break;
        }

        AR_DEBUG_PRINTF(ATH_PRINT_OUT_ZONE, ("Got %d messages on mailbox : %d \n",credits, mbox));

            /* get all the buffers that are sitting on the queue */
        for (i = 0; i < credits; i++) {
            A_ASSERT(curBuffer < BUFFER_PROC_LIST_DEPTH);
                /* recv the current buffer synchronously, the buffers should come back in
                 * order... with padding applied by the target */
            paddedLength = (recvList[curBuffer].length + (g_BlockSizes[mbox] - 1)) &
                       (~(g_BlockSizes[mbox] - 1));

            status = HIFReadWrite(pDev->HIFDevice,
                                  g_MailboxAddrs[mbox],
                                  recvList[curBuffer].pBuffer,
                                  paddedLength,
                                  request,
                                  NULL);
            if (status) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Failed to read %d bytes on mailbox:%d : address:0x%X \n",
                        recvList[curBuffer].length, mbox, g_MailboxAddrs[mbox]));
                break;
            }

            totalwPadding += paddedLength;
            totalBytes += recvList[curBuffer].length;
            curBuffer++;
        }

        if (status) {
            break;
        }
            /* go back and get some more */
        credits = 0;
    }

    if (totalBytes != TEST_BYTES) {
        A_ASSERT(false);
    }  else {
        AR_DEBUG_PRINTF(ATH_PRINT_OUT_ZONE, ("Got all buffers on mbox:%d total recv :%d (w/Padding : %d) \n",
            mbox, totalBytes, totalwPadding));
    }

    return status;


}

static int DoOneMboxHWTest(struct ar6k_device *pDev, int mbox)
{
    int status;

    do {
            /* send out buffers */
        status = SendBuffers(pDev,mbox);

        if (status) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Sending buffers Failed : %d mbox:%d\n",status,mbox));
            break;
        }

            /* go get them, this will block */
        status =  RecvBuffers(pDev, mbox);

        if (status) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Recv buffers Failed : %d mbox:%d\n",status,mbox));
            break;
        }

            /* check the returned data patterns */
        if (!CheckBuffers()) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Buffer Verify Failed : mbox:%d\n",mbox));
            status = A_ERROR;
            break;
        }

        AR_DEBUG_PRINTF(ATH_PRINT_OUT_ZONE, (" Send/Recv success! mailbox : %d \n",mbox));

    }  while (false);

    return status;
}

/* here is where the test starts */
int DoMboxHWTest(struct ar6k_device *pDev)
{
    int      i;
    int status;
    int      credits = 0;
    u8 params[4];
    int      numBufs;
    int      bufferSize;
    u16 temp;


    AR_DEBUG_PRINTF(ATH_PRINT_OUT_ZONE, (" DoMboxHWTest START -  \n"));

    do {
            /* get the addresses for all 4 mailboxes */
        status = HIFConfigureDevice(pDev->HIFDevice, HIF_DEVICE_GET_MBOX_ADDR,
                                    g_MailboxAddrs, sizeof(g_MailboxAddrs));

        if (status) {
            A_ASSERT(false);
            break;
        }

            /* get the block sizes */
        status = HIFConfigureDevice(pDev->HIFDevice, HIF_DEVICE_GET_MBOX_BLOCK_SIZE,
                                    g_BlockSizes, sizeof(g_BlockSizes));

        if (status) {
            A_ASSERT(false);
            break;
        }

            /* note, the HIF layer usually reports mbox 0 to have a block size of
             * 1, but our test wants to run in block-mode for all mailboxes, so we treat all mailboxes
             * the same. */
        g_BlockSizes[0] = g_BlockSizes[1];
        AR_DEBUG_PRINTF(ATH_PRINT_OUT_ZONE, ("Block Size to use: %d \n",g_BlockSizes[0]));

        if (g_BlockSizes[1] > BUFFER_BLOCK_PAD) {
            AR_DEBUG_PRINTF(ATH_PRINT_OUT_ZONE, ("%d Block size is too large for buffer pad %d\n",
                g_BlockSizes[1], BUFFER_BLOCK_PAD));
            break;
        }

        AR_DEBUG_PRINTF(ATH_PRINT_OUT_ZONE, ("Waiting for target.... \n"));

            /* the target lets us know it is ready by giving us 1 credit on
             * mailbox 0 */
        status = GetCredits(pDev, 0, &credits);

        if (status) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Failed to wait for target ready \n"));
            break;
        }

        AR_DEBUG_PRINTF(ATH_PRINT_OUT_ZONE, ("Target is ready ...\n"));

            /* read the first 4 scratch registers */
        status = HIFReadWrite(pDev->HIFDevice,
                              SCRATCH_ADDRESS,
                              params,
                              4,
                              HIF_RD_SYNC_BYTE_INC,
                              NULL);

        if (status) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Failed to wait get parameters \n"));
            break;
        }

        numBufs = params[0];
        bufferSize = (int)(((u16)params[2] << 8) | (u16)params[1]);

        AR_DEBUG_PRINTF(ATH_PRINT_OUT_ZONE,
            ("Target parameters: bufs per mailbox:%d, buffer size:%d bytes (total space: %d, minimum required space (w/padding): %d) \n",
            numBufs, bufferSize, (numBufs * bufferSize), TOTAL_BYTES));

        if ((numBufs * bufferSize) < TOTAL_BYTES) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Not Enough buffer space to run test! need:%d, got:%d \n",
                TOTAL_BYTES, (numBufs*bufferSize)));
            status = A_ERROR;
            break;
        }

        temp = GetEndMarker();

        status = HIFReadWrite(pDev->HIFDevice,
                              SCRATCH_ADDRESS + 4,
                              (u8 *)&temp,
                              2,
                              HIF_WR_SYNC_BYTE_INC,
                              NULL);

        if (status) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Failed to write end marker \n"));
            break;
        }

        AR_DEBUG_PRINTF(ATH_PRINT_OUT_ZONE, ("End Marker: 0x%X \n",temp));

        temp = (u16)g_BlockSizes[1];
            /* convert to a mask */
        temp = temp - 1;
        status = HIFReadWrite(pDev->HIFDevice,
                              SCRATCH_ADDRESS + 6,
                              (u8 *)&temp,
                              2,
                              HIF_WR_SYNC_BYTE_INC,
                              NULL);

        if (status) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Failed to write block mask \n"));
            break;
        }

        AR_DEBUG_PRINTF(ATH_PRINT_OUT_ZONE, ("Set Block Mask: 0x%X \n",temp));

            /* execute the test on each mailbox */
        for (i = 0; i < AR6K_MAILBOXES; i++) {
            status = DoOneMboxHWTest(pDev, i);
            if (status) {
                break;
            }
        }

    } while (false);

    if (status == 0) {
        AR_DEBUG_PRINTF(ATH_PRINT_OUT_ZONE, (" DoMboxHWTest DONE - SUCCESS! -  \n"));
    } else {
        AR_DEBUG_PRINTF(ATH_PRINT_OUT_ZONE, (" DoMboxHWTest DONE - FAILED! -  \n"));
    }
        /* don't let HTC_Start continue, the target is actually not running any HTC code */
    return A_ERROR;
}
#endif



