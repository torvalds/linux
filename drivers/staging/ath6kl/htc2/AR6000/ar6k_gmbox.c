//------------------------------------------------------------------------------
// <copyright file="ar6k_gmbox.c" company="Atheros">
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
// Generic MBOX API implementation
// 
// Author(s): ="Atheros"
//==============================================================================
#include "a_config.h"
#include "athdefs.h"
#include "a_types.h"
#include "a_osapi.h"
#include "../htc_debug.h"
#include "hif.h"
#include "htc_packet.h"
#include "ar6k.h"
#include "hw/mbox_host_reg.h"
#include "gmboxif.h"

/* 
 * This file provides management functions and a toolbox for GMBOX protocol modules.  
 * Only one protocol module can be installed at a time. The determination of which protocol
 * module is installed is determined at compile time.  
 * 
 */
#ifdef ATH_AR6K_ENABLE_GMBOX
     /* GMBOX definitions */
#define GMBOX_INT_STATUS_ENABLE_REG     0x488
#define GMBOX_INT_STATUS_RX_DATA        (1 << 0)
#define GMBOX_INT_STATUS_TX_OVERFLOW    (1 << 1)
#define GMBOX_INT_STATUS_RX_OVERFLOW    (1 << 2)

#define GMBOX_LOOKAHEAD_MUX_REG         0x498
#define GMBOX_LA_MUX_OVERRIDE_2_3       (1 << 0)

#define AR6K_GMBOX_CREDIT_DEC_ADDRESS   (COUNT_DEC_ADDRESS + 4 * AR6K_GMBOX_CREDIT_COUNTER)
#define AR6K_GMBOX_CREDIT_SIZE_ADDRESS  (COUNT_ADDRESS     + AR6K_GMBOX_CREDIT_SIZE_COUNTER)


    /* external APIs for allocating and freeing internal I/O packets to handle ASYNC I/O */ 
extern void AR6KFreeIOPacket(AR6K_DEVICE *pDev, HTC_PACKET *pPacket);
extern HTC_PACKET *AR6KAllocIOPacket(AR6K_DEVICE *pDev);


/* callback when our fetch to enable/disable completes */
static void DevGMboxIRQActionAsyncHandler(void *Context, HTC_PACKET *pPacket)
{
    AR6K_DEVICE *pDev = (AR6K_DEVICE *)Context;

    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("+DevGMboxIRQActionAsyncHandler: (dev: 0x%lX)\n", (unsigned long)pDev));

    if (A_FAILED(pPacket->Status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                ("IRQAction Operation (%d) failed! status:%d \n", pPacket->PktInfo.AsRx.HTCRxFlags,pPacket->Status));
    }
        /* free this IO packet */
    AR6KFreeIOPacket(pDev,pPacket);
    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("-DevGMboxIRQActionAsyncHandler \n"));
}

static A_STATUS DevGMboxCounterEnableDisable(AR6K_DEVICE *pDev, GMBOX_IRQ_ACTION_TYPE IrqAction, A_BOOL AsyncMode)
{
    A_STATUS                  status = A_OK;
    AR6K_IRQ_ENABLE_REGISTERS regs;
    HTC_PACKET                *pIOPacket = NULL;  
    
    LOCK_AR6K(pDev);
    
    if (GMBOX_CREDIT_IRQ_ENABLE == IrqAction) {
        pDev->GMboxInfo.CreditCountIRQEnabled = TRUE;
        pDev->IrqEnableRegisters.counter_int_status_enable |=
            COUNTER_INT_STATUS_ENABLE_BIT_SET(1 << AR6K_GMBOX_CREDIT_COUNTER);
        pDev->IrqEnableRegisters.int_status_enable |= INT_STATUS_ENABLE_COUNTER_SET(0x01);
    } else {
        pDev->GMboxInfo.CreditCountIRQEnabled = FALSE;
        pDev->IrqEnableRegisters.counter_int_status_enable &=
            ~(COUNTER_INT_STATUS_ENABLE_BIT_SET(1 << AR6K_GMBOX_CREDIT_COUNTER));    
    }
        /* copy into our temp area */
    A_MEMCPY(&regs,&pDev->IrqEnableRegisters,AR6K_IRQ_ENABLE_REGS_SIZE);

    UNLOCK_AR6K(pDev);

    do {

        if (AsyncMode) {

            pIOPacket = AR6KAllocIOPacket(pDev);

            if (NULL == pIOPacket) {
                status = A_NO_MEMORY;
                A_ASSERT(FALSE);
                break;
            }

                /* copy values to write to our async I/O buffer */
            A_MEMCPY(pIOPacket->pBuffer,&pDev->IrqEnableRegisters,AR6K_IRQ_ENABLE_REGS_SIZE);

                /* stick in our completion routine when the I/O operation completes */
            pIOPacket->Completion = DevGMboxIRQActionAsyncHandler;
            pIOPacket->pContext = pDev;
            pIOPacket->PktInfo.AsRx.HTCRxFlags = IrqAction;
                /* write it out asynchronously */
            HIFReadWrite(pDev->HIFDevice,
                         INT_STATUS_ENABLE_ADDRESS,
                         pIOPacket->pBuffer,
                         AR6K_IRQ_ENABLE_REGS_SIZE,
                         HIF_WR_ASYNC_BYTE_INC,
                         pIOPacket);
                         
            pIOPacket = NULL; 
            break;
        } 

            /* if we get here we are doing it synchronously */
        status = HIFReadWrite(pDev->HIFDevice,
                              INT_STATUS_ENABLE_ADDRESS,
                              &regs.int_status_enable,
                              AR6K_IRQ_ENABLE_REGS_SIZE,
                              HIF_WR_SYNC_BYTE_INC,
                              NULL);    
    } while (FALSE);
    
    if (A_FAILED(status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                (" IRQAction Operation (%d) failed! status:%d \n", IrqAction, status));    
    } else {
        if (!AsyncMode) {
            AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,
                    (" IRQAction Operation (%d) success \n", IrqAction)); 
        }       
    }
    
    if (pIOPacket != NULL) {
        AR6KFreeIOPacket(pDev,pIOPacket);
    }
        
    return status;
}


A_STATUS DevGMboxIRQAction(AR6K_DEVICE *pDev, GMBOX_IRQ_ACTION_TYPE IrqAction, A_BOOL AsyncMode)
{
    A_STATUS      status = A_OK;
    HTC_PACKET    *pIOPacket = NULL;   
    A_UINT8       GMboxIntControl[4];

    if (GMBOX_CREDIT_IRQ_ENABLE == IrqAction) {
        return DevGMboxCounterEnableDisable(pDev, GMBOX_CREDIT_IRQ_ENABLE, AsyncMode);
    } else if(GMBOX_CREDIT_IRQ_DISABLE == IrqAction) {
        return DevGMboxCounterEnableDisable(pDev, GMBOX_CREDIT_IRQ_DISABLE, AsyncMode);
    }
    
    if (GMBOX_DISABLE_ALL == IrqAction) {
            /* disable credit IRQ, those are on a different set of registers */
        DevGMboxCounterEnableDisable(pDev, GMBOX_CREDIT_IRQ_DISABLE, AsyncMode);    
    }
            
        /* take the lock to protect interrupt enable shadows */
    LOCK_AR6K(pDev);

    switch (IrqAction) {
        
        case GMBOX_DISABLE_ALL:
            pDev->GMboxControlRegisters.int_status_enable = 0;
            break;
        case GMBOX_ERRORS_IRQ_ENABLE:
            pDev->GMboxControlRegisters.int_status_enable |= GMBOX_INT_STATUS_TX_OVERFLOW |
                                                             GMBOX_INT_STATUS_RX_OVERFLOW;
            break;
        case GMBOX_RECV_IRQ_ENABLE:
            pDev->GMboxControlRegisters.int_status_enable |= GMBOX_INT_STATUS_RX_DATA;
            break;
        case GMBOX_RECV_IRQ_DISABLE:
            pDev->GMboxControlRegisters.int_status_enable &= ~GMBOX_INT_STATUS_RX_DATA;
            break;
        case GMBOX_ACTION_NONE:
        default:
            A_ASSERT(FALSE);    
            break;
    }
    
    GMboxIntControl[0] = pDev->GMboxControlRegisters.int_status_enable;
    GMboxIntControl[1] = GMboxIntControl[0];
    GMboxIntControl[2] = GMboxIntControl[0];
    GMboxIntControl[3] = GMboxIntControl[0];
    
    UNLOCK_AR6K(pDev);

    do {

        if (AsyncMode) {

            pIOPacket = AR6KAllocIOPacket(pDev);

            if (NULL == pIOPacket) {
                status = A_NO_MEMORY;
                A_ASSERT(FALSE);
                break;
            }

                /* copy values to write to our async I/O buffer */
            A_MEMCPY(pIOPacket->pBuffer,GMboxIntControl,sizeof(GMboxIntControl));

                /* stick in our completion routine when the I/O operation completes */
            pIOPacket->Completion = DevGMboxIRQActionAsyncHandler;
            pIOPacket->pContext = pDev;
            pIOPacket->PktInfo.AsRx.HTCRxFlags = IrqAction;
                /* write it out asynchronously */
            HIFReadWrite(pDev->HIFDevice,
                         GMBOX_INT_STATUS_ENABLE_REG,
                         pIOPacket->pBuffer,
                         sizeof(GMboxIntControl),
                         HIF_WR_ASYNC_BYTE_FIX,
                         pIOPacket);
            pIOPacket = NULL;
            break;
        }

        /* if we get here we are doing it synchronously */

        status = HIFReadWrite(pDev->HIFDevice,
                              GMBOX_INT_STATUS_ENABLE_REG,
                              GMboxIntControl,
                              sizeof(GMboxIntControl),
                              HIF_WR_SYNC_BYTE_FIX,
                              NULL);

    } while (FALSE);

    if (A_FAILED(status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                (" IRQAction Operation (%d) failed! status:%d \n", IrqAction, status));    
    } else {
        if (!AsyncMode) {
            AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,
                    (" IRQAction Operation (%d) success \n", IrqAction)); 
        }      
    }
    
    if (pIOPacket != NULL) {
        AR6KFreeIOPacket(pDev,pIOPacket);
    }

    return status;
}

void DevCleanupGMbox(AR6K_DEVICE *pDev)
{
    if (pDev->GMboxEnabled) {
        pDev->GMboxEnabled = FALSE;
        GMboxProtocolUninstall(pDev);        
    }
}

A_STATUS DevSetupGMbox(AR6K_DEVICE *pDev)
{
    A_STATUS    status = A_OK;
    A_UINT8     muxControl[4];
    
    do {
        
        if (0 == pDev->MailBoxInfo.GMboxAddress) {
            break;    
        }
    
        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,(" GMBOX Advertised: Address:0x%X , size:%d \n",
                    pDev->MailBoxInfo.GMboxAddress, pDev->MailBoxInfo.GMboxSize));
                    
        status = DevGMboxIRQAction(pDev, GMBOX_DISABLE_ALL, PROC_IO_SYNC);
        
        if (A_FAILED(status)) {
            break;    
        }
       
            /* write to mailbox look ahead mux control register, we want the
             * GMBOX lookaheads to appear on lookaheads 2 and 3 
             * the register is 1-byte wide so we need to hit it 4 times to align the operation 
             * to 4-bytes */            
        muxControl[0] = GMBOX_LA_MUX_OVERRIDE_2_3;
        muxControl[1] = GMBOX_LA_MUX_OVERRIDE_2_3;
        muxControl[2] = GMBOX_LA_MUX_OVERRIDE_2_3;
        muxControl[3] = GMBOX_LA_MUX_OVERRIDE_2_3;
                
        status = HIFReadWrite(pDev->HIFDevice,
                              GMBOX_LOOKAHEAD_MUX_REG,
                              muxControl,
                              sizeof(muxControl),
                              HIF_WR_SYNC_BYTE_FIX,  /* hit this register 4 times */
                              NULL);
        
        if (A_FAILED(status)) {
            break;    
        }
        
        status = GMboxProtocolInstall(pDev);
        
        if (A_FAILED(status)) {
            break;    
        }
        
        pDev->GMboxEnabled = TRUE;
        
    } while (FALSE);
    
    return status;
}

A_STATUS DevCheckGMboxInterrupts(AR6K_DEVICE *pDev)
{
    A_STATUS status = A_OK;
    A_UINT8  counter_int_status;
    int      credits;
    A_UINT8  host_int_status2;
    
    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ, ("+DevCheckGMboxInterrupts \n"));
     
    /* the caller guarantees that this is a context that allows for blocking I/O */
    
    do {
        
        host_int_status2 = pDev->IrqProcRegisters.host_int_status2 &
                           pDev->GMboxControlRegisters.int_status_enable; 
                
        if (host_int_status2 & GMBOX_INT_STATUS_TX_OVERFLOW) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("GMBOX : TX Overflow \n"));  
            status = A_ECOMM;   
        }
        
        if (host_int_status2 & GMBOX_INT_STATUS_RX_OVERFLOW) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("GMBOX : RX Overflow \n"));  
            status = A_ECOMM;    
        }
        
        if (A_FAILED(status)) {
            if (pDev->GMboxInfo.pTargetFailureCallback != NULL) {
                pDev->GMboxInfo.pTargetFailureCallback(pDev->GMboxInfo.pProtocolContext, status);        
            }
            break;
        }
    
        if (host_int_status2 & GMBOX_INT_STATUS_RX_DATA) {
            if (pDev->IrqProcRegisters.gmbox_rx_avail > 0) {
                A_ASSERT(pDev->GMboxInfo.pMessagePendingCallBack != NULL);
                status = pDev->GMboxInfo.pMessagePendingCallBack(
                                pDev->GMboxInfo.pProtocolContext,
                                (A_UINT8 *)&pDev->IrqProcRegisters.rx_gmbox_lookahead_alias[0],
                                pDev->IrqProcRegisters.gmbox_rx_avail);
            }
        } 
        
        if (A_FAILED(status)) {
           break;                
        }
        
        counter_int_status = pDev->IrqProcRegisters.counter_int_status &
                             pDev->IrqEnableRegisters.counter_int_status_enable;
    
            /* check if credit interrupt is pending */
        if (counter_int_status & (COUNTER_INT_STATUS_ENABLE_BIT_SET(1 << AR6K_GMBOX_CREDIT_COUNTER))) {
            
                /* do synchronous read */
            status = DevGMboxReadCreditCounter(pDev, PROC_IO_SYNC, &credits);
            
            if (A_FAILED(status)) {
                break;    
            }
            
            A_ASSERT(pDev->GMboxInfo.pCreditsPendingCallback != NULL);
            status = pDev->GMboxInfo.pCreditsPendingCallback(pDev->GMboxInfo.pProtocolContext,
                                                             credits,
                                                             pDev->GMboxInfo.CreditCountIRQEnabled);
        }
        
    } while (FALSE);
    
    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ, ("-DevCheckGMboxInterrupts (%d) \n",status));
    
    return status;
}


A_STATUS DevGMboxWrite(AR6K_DEVICE *pDev, HTC_PACKET *pPacket, A_UINT32 WriteLength) 
{
    A_UINT32 paddedLength;
    A_BOOL   sync = (pPacket->Completion == NULL) ? TRUE : FALSE;
    A_STATUS status;
    A_UINT32 address;
    
       /* adjust the length to be a multiple of block size if appropriate */
    paddedLength = DEV_CALC_SEND_PADDED_LEN(pDev, WriteLength);
    
    AR_DEBUG_PRINTF(ATH_DEBUG_SEND,
                ("DevGMboxWrite, Padded Length: %d Mbox:0x%X (mode:%s)\n",
                WriteLength,
                pDev->MailBoxInfo.GMboxAddress,
                sync ? "SYNC" : "ASYNC"));
                
        /* last byte of packet has to hit the EOM marker */
    address = pDev->MailBoxInfo.GMboxAddress + pDev->MailBoxInfo.GMboxSize - paddedLength;
    
    status = HIFReadWrite(pDev->HIFDevice,
                          address,
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

A_STATUS DevGMboxRead(AR6K_DEVICE *pDev, HTC_PACKET *pPacket, A_UINT32 ReadLength) 
{
    
    A_UINT32 paddedLength;
    A_STATUS status;
    A_BOOL   sync = (pPacket->Completion == NULL) ? TRUE : FALSE;

        /* adjust the length to be a multiple of block size if appropriate */
    paddedLength = DEV_CALC_RECV_PADDED_LEN(pDev, ReadLength);
                    
    if (paddedLength > pPacket->BufferLength) {
        A_ASSERT(FALSE);
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                ("DevGMboxRead, Not enough space for padlen:%d recvlen:%d bufferlen:%d \n",
                    paddedLength,ReadLength,pPacket->BufferLength));
        if (pPacket->Completion != NULL) {
            COMPLETE_HTC_PACKET(pPacket,A_EINVAL);
            return A_OK;
        }
        return A_EINVAL;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,
                ("DevGMboxRead (0x%lX : hdr:0x%X) Padded Length: %d Mbox:0x%X (mode:%s)\n",
                (unsigned long)pPacket, pPacket->PktInfo.AsRx.ExpectedHdr,
                paddedLength,
                pDev->MailBoxInfo.GMboxAddress,
                sync ? "SYNC" : "ASYNC"));

    status = HIFReadWrite(pDev->HIFDevice,
                          pDev->MailBoxInfo.GMboxAddress,
                          pPacket->pBuffer,
                          paddedLength,
                          sync ? HIF_RD_SYNC_BLOCK_FIX : HIF_RD_ASYNC_BLOCK_FIX,
                          sync ? NULL : pPacket);  /* pass the packet as the context to the HIF request */

    if (sync) {
        pPacket->Status = status;
    }

    return status;
}


static int ProcessCreditCounterReadBuffer(A_UINT8 *pBuffer, int Length)
{
    int     credits = 0;
    
    /* theory of how this works:
     * We read the credit decrement register multiple times on a byte-wide basis. 
     * The number of times (32) aligns the I/O operation to be a multiple of 4 bytes and provides a 
     * reasonable chance to acquire "all" pending credits in a single I/O operation. 
     * 
     * Once we obtain the filled buffer, we can walk through it looking for credit decrement transitions.
     * Each non-zero byte represents a single credit decrement (which is a credit given back to the host)
     * For example if the target provides 3 credits and added 4 more during the 32-byte read operation the following
     * pattern "could" appear:
     * 
     *    0x3 0x2 0x1 0x0 0x0 0x0 0x0 0x0 0x1 0x0 0x1 0x0 0x1 0x0 0x1 0x0 ......rest zeros
     *    <--------->                     <----------------------------->
     *         \_ credits aleady there              \_ target adding 4 more credits
     * 
     *    The total available credits would be 7, since there are 7 non-zero bytes in the buffer.
     * 
     * */
    
    if (AR_DEBUG_LVL_CHECK(ATH_DEBUG_RECV)) {
        DebugDumpBytes(pBuffer, Length, "GMBOX Credit read buffer");
    } 
        
    while (Length) {
        if (*pBuffer != 0) {
            credits++;    
        }
        Length--;
        pBuffer++;   
    }  
    
    return credits;
}
   

/* callback when our fetch to enable/disable completes */
static void DevGMboxReadCreditsAsyncHandler(void *Context, HTC_PACKET *pPacket)
{
    AR6K_DEVICE *pDev = (AR6K_DEVICE *)Context;

    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("+DevGMboxReadCreditsAsyncHandler: (dev: 0x%lX)\n", (unsigned long)pDev));

    if (A_FAILED(pPacket->Status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                ("Read Credit Operation failed! status:%d \n", pPacket->Status));
    } else {
        int credits = 0;
        credits = ProcessCreditCounterReadBuffer(pPacket->pBuffer, AR6K_REG_IO_BUFFER_SIZE);
        pDev->GMboxInfo.pCreditsPendingCallback(pDev->GMboxInfo.pProtocolContext,
                                                credits,
                                                pDev->GMboxInfo.CreditCountIRQEnabled);
        
        
    }
        /* free this IO packet */
    AR6KFreeIOPacket(pDev,pPacket);
    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("-DevGMboxReadCreditsAsyncHandler \n"));
}

A_STATUS DevGMboxReadCreditCounter(AR6K_DEVICE *pDev, A_BOOL AsyncMode, int *pCredits)
{
    A_STATUS    status = A_OK;
    HTC_PACKET  *pIOPacket = NULL;  
    
    AR_DEBUG_PRINTF(ATH_DEBUG_SEND,("+DevGMboxReadCreditCounter (%s) \n", AsyncMode ? "ASYNC" : "SYNC"));
                                            
    do {
        
        pIOPacket = AR6KAllocIOPacket(pDev);

        if (NULL == pIOPacket) {
            status = A_NO_MEMORY;
            A_ASSERT(FALSE);
            break;
        }
        
        A_MEMZERO(pIOPacket->pBuffer,AR6K_REG_IO_BUFFER_SIZE);
      
        if (AsyncMode) {   
                /* stick in our completion routine when the I/O operation completes */
            pIOPacket->Completion = DevGMboxReadCreditsAsyncHandler;
            pIOPacket->pContext = pDev;
                /* read registers asynchronously */
            HIFReadWrite(pDev->HIFDevice,
                         AR6K_GMBOX_CREDIT_DEC_ADDRESS,
                         pIOPacket->pBuffer,
                         AR6K_REG_IO_BUFFER_SIZE,  /* hit the register multiple times */
                         HIF_RD_ASYNC_BYTE_FIX,
                         pIOPacket);
            pIOPacket = NULL;
            break;
        } 

        pIOPacket->Completion = NULL;
            /* if we get here we are doing it synchronously */
        status = HIFReadWrite(pDev->HIFDevice,
                              AR6K_GMBOX_CREDIT_DEC_ADDRESS,
                              pIOPacket->pBuffer,
                              AR6K_REG_IO_BUFFER_SIZE,
                              HIF_RD_SYNC_BYTE_FIX,
                              NULL);    
    } while (FALSE);
    
    if (A_FAILED(status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                (" DevGMboxReadCreditCounter failed! status:%d \n", status));          
    }
    
    if (pIOPacket != NULL) {
        if (A_SUCCESS(status)) {
                /* sync mode processing */
            *pCredits = ProcessCreditCounterReadBuffer(pIOPacket->pBuffer, AR6K_REG_IO_BUFFER_SIZE);     
        }
        AR6KFreeIOPacket(pDev,pIOPacket);
    }
    
    AR_DEBUG_PRINTF(ATH_DEBUG_SEND,("-DevGMboxReadCreditCounter (%s) (%d) \n", 
            AsyncMode ? "ASYNC" : "SYNC", status));
    
    return status;
}

A_STATUS DevGMboxReadCreditSize(AR6K_DEVICE *pDev, int *pCreditSize)
{
    A_STATUS    status;
    A_UINT8     buffer[4];
       
    status = HIFReadWrite(pDev->HIFDevice,
                          AR6K_GMBOX_CREDIT_SIZE_ADDRESS,
                          buffer,
                          sizeof(buffer),
                          HIF_RD_SYNC_BYTE_FIX, /* hit the register 4 times to align the I/O */
                          NULL);    
    
    if (A_SUCCESS(status)) {
        if (buffer[0] == 0) {
            *pCreditSize = 256;    
        } else {   
            *pCreditSize = buffer[0];
        } 
           
    } 
    
    return status;
}

void DevNotifyGMboxTargetFailure(AR6K_DEVICE *pDev)
{
        /* Target ASSERTED!!! */
    if (pDev->GMboxInfo.pTargetFailureCallback != NULL) {
        pDev->GMboxInfo.pTargetFailureCallback(pDev->GMboxInfo.pProtocolContext, A_HARDWARE);        
    }
}

A_STATUS DevGMboxRecvLookAheadPeek(AR6K_DEVICE *pDev, A_UINT8 *pLookAheadBuffer, int *pLookAheadBytes)
{

    A_STATUS                    status = A_OK;
    AR6K_IRQ_PROC_REGISTERS     procRegs;
    int                         maxCopy;
  
    do {
            /* on entry the caller provides the length of the lookahead buffer */
        if (*pLookAheadBytes > sizeof(procRegs.rx_gmbox_lookahead_alias)) {
            A_ASSERT(FALSE);
            status = A_EINVAL;
            break;    
        }
        
        maxCopy = *pLookAheadBytes;
        *pLookAheadBytes = 0;
            /* load the register table from the device */
        status = HIFReadWrite(pDev->HIFDevice,
                              HOST_INT_STATUS_ADDRESS,
                              (A_UINT8 *)&procRegs,
                              AR6K_IRQ_PROC_REGS_SIZE,
                              HIF_RD_SYNC_BYTE_INC,
                              NULL);

        if (A_FAILED(status)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                ("DevGMboxRecvLookAheadPeek : Failed to read register table (%d) \n",status));
            break;
        }
        
        if (procRegs.gmbox_rx_avail > 0) {
            int bytes = procRegs.gmbox_rx_avail > maxCopy ? maxCopy : procRegs.gmbox_rx_avail;
            A_MEMCPY(pLookAheadBuffer,&procRegs.rx_gmbox_lookahead_alias[0],bytes);
            *pLookAheadBytes = bytes;
        }
        
    } while (FALSE);
       
    return status; 
}

A_STATUS DevGMboxSetTargetInterrupt(AR6K_DEVICE *pDev, int Signal, int AckTimeoutMS)
{
    A_STATUS status = A_OK;
    int      i;
    A_UINT8  buffer[4];
    
    A_MEMZERO(buffer, sizeof(buffer));
    
    do {
        
        if (Signal >= MBOX_SIG_HCI_BRIDGE_MAX) {
            status = A_EINVAL;
            break;    
        }
        
            /* set the last buffer to do the actual signal trigger */
        buffer[3] = (1 << Signal);
        
        status = HIFReadWrite(pDev->HIFDevice,
                              INT_WLAN_ADDRESS,
                              buffer,
                              sizeof(buffer),
                              HIF_WR_SYNC_BYTE_FIX, /* hit the register 4 times to align the I/O */
                              NULL);    
                          
        if (A_FAILED(status)) {
            break;    
        }
        
    } while (FALSE);
    
    
    if (A_SUCCESS(status)) {        
            /* now read back the register to see if the bit cleared */
        while (AckTimeoutMS) {        
            status = HIFReadWrite(pDev->HIFDevice,
                                  INT_WLAN_ADDRESS,
                                  buffer,
                                  sizeof(buffer),
                                  HIF_RD_SYNC_BYTE_FIX,
                                  NULL);    
                          
            if (A_FAILED(status)) {
                break;    
            }
                            
            for (i = 0; i < sizeof(buffer); i++) {
                if (buffer[i] & (1 << Signal)) {
                    /* bit is still set */
                    break;    
                }   
            }
            
            if (i >= sizeof(buffer)) {
                /* done */
                break;    
            }
            
            AckTimeoutMS--;
            A_MDELAY(1);  
        }
        
        if (0 == AckTimeoutMS) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                ("DevGMboxSetTargetInterrupt : Ack Timed-out (sig:%d) \n",Signal));
            status = A_ERROR;    
        }        
    }
    
    return status;
    
}

#endif  //ATH_AR6K_ENABLE_GMBOX




