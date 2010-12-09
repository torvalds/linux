//------------------------------------------------------------------------------
// <copyright file="ar6k_events.c" company="Atheros">
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
// AR6K Driver layer event handling (i.e. interrupts, message polling)
//
// Author(s): ="Atheros"
//==============================================================================

#include "a_config.h"
#include "athdefs.h"
#include "a_types.h"
#include "AR6002/hw2.0/hw/mbox_host_reg.h"
#include "a_osapi.h"
#include "../htc_debug.h"
#include "hif.h"
#include "htc_packet.h"
#include "ar6k.h"

extern void AR6KFreeIOPacket(AR6K_DEVICE *pDev, HTC_PACKET *pPacket);
extern HTC_PACKET *AR6KAllocIOPacket(AR6K_DEVICE *pDev);

static A_STATUS DevServiceDebugInterrupt(AR6K_DEVICE *pDev);

#define DELAY_PER_INTERVAL_MS 10  /* 10 MS delay per polling interval */

/* completion routine for ALL HIF layer async I/O */
A_STATUS DevRWCompletionHandler(void *context, A_STATUS status)
{
    HTC_PACKET *pPacket = (HTC_PACKET *)context;

    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,
                ("+DevRWCompletionHandler (Pkt:0x%lX) , Status: %d \n",
                (unsigned long)pPacket,
                status));

    COMPLETE_HTC_PACKET(pPacket,status);

    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,
                ("-DevRWCompletionHandler\n"));

    return A_OK;
}

/* mailbox recv message polling */
A_STATUS DevPollMboxMsgRecv(AR6K_DEVICE *pDev,
                            A_UINT32    *pLookAhead,
                            int          TimeoutMS)
{
    A_STATUS status = A_OK;
    int      timeout = TimeoutMS/DELAY_PER_INTERVAL_MS;

    A_ASSERT(timeout > 0);

    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,("+DevPollMboxMsgRecv \n"));

    while (TRUE) {

        if (pDev->GetPendingEventsFunc != NULL) {

            HIF_PENDING_EVENTS_INFO events;

#ifdef THREAD_X
			events.Polling =1;
#endif

            /* the HIF layer uses a special mechanism to get events, do this
             * synchronously */
            status = pDev->GetPendingEventsFunc(pDev->HIFDevice,
                                            &events,
                                            NULL);
            if (A_FAILED(status))
            {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Failed to get pending events \n"));
                break;
            }

            if (events.Events & HIF_RECV_MSG_AVAIL)
            {
                    /*  there is a message available, the lookahead should be valid now */
                *pLookAhead = events.LookAhead;

                break;
            }
        } else {

                /* this is the standard HIF way.... */
                /* load the register table */
            status = HIFReadWrite(pDev->HIFDevice,
                                  HOST_INT_STATUS_ADDRESS,
                                  (A_UINT8 *)&pDev->IrqProcRegisters,
                                  AR6K_IRQ_PROC_REGS_SIZE,
                                  HIF_RD_SYNC_BYTE_INC,
                                  NULL);

            if (A_FAILED(status)){
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Failed to read register table \n"));
                break;
            }

                /* check for MBOX data and valid lookahead */
            if (pDev->IrqProcRegisters.host_int_status & (1 << HTC_MAILBOX)) {
                if (pDev->IrqProcRegisters.rx_lookahead_valid & (1 << HTC_MAILBOX))
                {
                    /* mailbox has a message and the look ahead is valid */
                    *pLookAhead = pDev->IrqProcRegisters.rx_lookahead[HTC_MAILBOX];
                    break;
                }
            }

        }

        timeout--;

        if (timeout <= 0) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, (" Timeout waiting for recv message \n"));
            status = A_ERROR;

                /* check if the target asserted */
            if ( pDev->IrqProcRegisters.counter_int_status & AR6K_TARGET_DEBUG_INTR_MASK) {
                    /* target signaled an assert, process this pending interrupt
                     * this will call the target failure handler */
                DevServiceDebugInterrupt(pDev);
            }

            break;
        }

            /* delay a little  */
        A_MDELAY(DELAY_PER_INTERVAL_MS);
        AR_DEBUG_PRINTF(ATH_DEBUG_RECV,("  Retry Mbox Poll : %d \n",timeout));
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_RECV,("-DevPollMboxMsgRecv \n"));

    return status;
}

static A_STATUS DevServiceCPUInterrupt(AR6K_DEVICE *pDev)
{
    A_STATUS status;
    A_UINT8  cpu_int_status;
    A_UINT8  regBuffer[4];

    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ, ("CPU Interrupt\n"));
    cpu_int_status = pDev->IrqProcRegisters.cpu_int_status &
                     pDev->IrqEnableRegisters.cpu_int_status_enable;
    A_ASSERT(cpu_int_status);
    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,
                    ("Valid interrupt source(s) in CPU_INT_STATUS: 0x%x\n",
                    cpu_int_status));

        /* Clear the interrupt */
    pDev->IrqProcRegisters.cpu_int_status &= ~cpu_int_status; /* W1C */

        /* set up the register transfer buffer to hit the register 4 times , this is done
         * to make the access 4-byte aligned to mitigate issues with host bus interconnects that
         * restrict bus transfer lengths to be a multiple of 4-bytes */

        /* set W1C value to clear the interrupt, this hits the register first */
    regBuffer[0] = cpu_int_status;
        /* the remaining 4 values are set to zero which have no-effect  */
    regBuffer[1] = 0;
    regBuffer[2] = 0;
    regBuffer[3] = 0;

    status = HIFReadWrite(pDev->HIFDevice,
                          CPU_INT_STATUS_ADDRESS,
                          regBuffer,
                          4,
                          HIF_WR_SYNC_BYTE_FIX,
                          NULL);

    A_ASSERT(status == A_OK);
    return status;
}


static A_STATUS DevServiceErrorInterrupt(AR6K_DEVICE *pDev)
{
    A_STATUS status;
    A_UINT8  error_int_status;
    A_UINT8  regBuffer[4];

    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ, ("Error Interrupt\n"));
    error_int_status = pDev->IrqProcRegisters.error_int_status & 0x0F;
    A_ASSERT(error_int_status);
    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,
                    ("Valid interrupt source(s) in ERROR_INT_STATUS: 0x%x\n",
                    error_int_status));

    if (ERROR_INT_STATUS_WAKEUP_GET(error_int_status)) {
        /* Wakeup */
        AR_DEBUG_PRINTF(ATH_DEBUG_IRQ, ("Error : Wakeup\n"));
    }

    if (ERROR_INT_STATUS_RX_UNDERFLOW_GET(error_int_status)) {
        /* Rx Underflow */
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Error : Rx Underflow\n"));
    }

    if (ERROR_INT_STATUS_TX_OVERFLOW_GET(error_int_status)) {
        /* Tx Overflow */
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Error : Tx Overflow\n"));
    }

        /* Clear the interrupt */
    pDev->IrqProcRegisters.error_int_status &= ~error_int_status; /* W1C */

        /* set up the register transfer buffer to hit the register 4 times , this is done
         * to make the access 4-byte aligned to mitigate issues with host bus interconnects that
         * restrict bus transfer lengths to be a multiple of 4-bytes */

        /* set W1C value to clear the interrupt, this hits the register first */
    regBuffer[0] = error_int_status;
        /* the remaining 4 values are set to zero which have no-effect  */
    regBuffer[1] = 0;
    regBuffer[2] = 0;
    regBuffer[3] = 0;

    status = HIFReadWrite(pDev->HIFDevice,
                          ERROR_INT_STATUS_ADDRESS,
                          regBuffer,
                          4,
                          HIF_WR_SYNC_BYTE_FIX,
                          NULL);

    A_ASSERT(status == A_OK);
    return status;
}

static A_STATUS DevServiceDebugInterrupt(AR6K_DEVICE *pDev)
{
    A_UINT32 dummy;
    A_STATUS status;

    /* Send a target failure event to the application */
    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Target debug interrupt\n"));

    if (pDev->TargetFailureCallback != NULL) {
        pDev->TargetFailureCallback(pDev->HTCContext);
    }

    if (pDev->GMboxEnabled) {
        DevNotifyGMboxTargetFailure(pDev);
    }

    /* clear the interrupt , the debug error interrupt is
     * counter 0 */
        /* read counter to clear interrupt */
    status = HIFReadWrite(pDev->HIFDevice,
                          COUNT_DEC_ADDRESS,
                          (A_UINT8 *)&dummy,
                          4,
                          HIF_RD_SYNC_BYTE_INC,
                          NULL);

    A_ASSERT(status == A_OK);
    return status;
}

static A_STATUS DevServiceCounterInterrupt(AR6K_DEVICE *pDev)
{
    A_UINT8 counter_int_status;

    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ, ("Counter Interrupt\n"));

    counter_int_status = pDev->IrqProcRegisters.counter_int_status &
                         pDev->IrqEnableRegisters.counter_int_status_enable;

    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,
                    ("Valid interrupt source(s) in COUNTER_INT_STATUS: 0x%x\n",
                    counter_int_status));

        /* Check if the debug interrupt is pending
         * NOTE: other modules like GMBOX may use the counter interrupt for
         * credit flow control on other counters, we only need to check for the debug assertion
         * counter interrupt */
    if (counter_int_status & AR6K_TARGET_DEBUG_INTR_MASK) {
        return DevServiceDebugInterrupt(pDev);
    }

    return A_OK;
}

/* callback when our fetch to get interrupt status registers completes */
static void DevGetEventAsyncHandler(void *Context, HTC_PACKET *pPacket)
{
    AR6K_DEVICE *pDev = (AR6K_DEVICE *)Context;
    A_UINT32    lookAhead = 0;
    A_BOOL      otherInts = FALSE;

    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("+DevGetEventAsyncHandler: (dev: 0x%lX)\n", (unsigned long)pDev));

    do {

        if (A_FAILED(pPacket->Status)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                    (" GetEvents I/O request failed, status:%d \n", pPacket->Status));
            /* bail out, don't unmask HIF interrupt */
            break;
        }

        if (pDev->GetPendingEventsFunc != NULL) {
                /* the HIF layer collected the information for us */
            HIF_PENDING_EVENTS_INFO *pEvents = (HIF_PENDING_EVENTS_INFO *)pPacket->pBuffer;
            if (pEvents->Events & HIF_RECV_MSG_AVAIL) {
                lookAhead = pEvents->LookAhead;
                if (0 == lookAhead) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,(" DevGetEventAsyncHandler1, lookAhead is zero! \n"));
                }
            }
            if (pEvents->Events & HIF_OTHER_EVENTS) {
                otherInts = TRUE;
            }
        } else {
                /* standard interrupt table handling.... */
            AR6K_IRQ_PROC_REGISTERS *pReg = (AR6K_IRQ_PROC_REGISTERS *)pPacket->pBuffer;
            A_UINT8                 host_int_status;

            host_int_status = pReg->host_int_status & pDev->IrqEnableRegisters.int_status_enable;

            if (host_int_status & (1 << HTC_MAILBOX)) {
                host_int_status &= ~(1 << HTC_MAILBOX);
                if (pReg->rx_lookahead_valid & (1 << HTC_MAILBOX)) {
                        /* mailbox has a message and the look ahead is valid */
                    lookAhead = pReg->rx_lookahead[HTC_MAILBOX];
                    if (0 == lookAhead) {
                        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,(" DevGetEventAsyncHandler2, lookAhead is zero! \n"));
                    }
                }
            }

            if (host_int_status) {
                    /* there are other interrupts to handle */
                otherInts = TRUE;
            }
        }

        if (otherInts || (lookAhead == 0)) {
            /* if there are other interrupts to process, we cannot do this in the async handler so
             * ack the interrupt which will cause our sync handler to run again
             * if however there are no more messages, we can now ack the interrupt  */
            AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,
                (" Acking interrupt from DevGetEventAsyncHandler (otherints:%d, lookahead:0x%X)\n",
                otherInts, lookAhead));
            HIFAckInterrupt(pDev->HIFDevice);
        } else {
            int      fetched = 0;
            A_STATUS status;

            AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,
                    (" DevGetEventAsyncHandler : detected another message, lookahead :0x%X \n",
                    lookAhead));
                /* lookahead is non-zero and there are no other interrupts to service,
                 * go get the next message */
            status = pDev->MessagePendingCallback(pDev->HTCContext, &lookAhead, 1, NULL, &fetched);

            if (A_SUCCESS(status) && !fetched) {
                    /* HTC layer could not pull out messages due to lack of resources, stop IRQ processing */
                AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("MessagePendingCallback did not pull any messages, force-ack \n"));
                DevAsyncIrqProcessComplete(pDev);
            }
        }

    } while (FALSE);

        /* free this IO packet */
    AR6KFreeIOPacket(pDev,pPacket);
    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("-DevGetEventAsyncHandler \n"));
}

/* called by the HTC layer when it wants us to check if the device has any more pending
 * recv messages, this starts off a series of async requests to read interrupt registers  */
A_STATUS DevCheckPendingRecvMsgsAsync(void *context)
{
    AR6K_DEVICE  *pDev = (AR6K_DEVICE *)context;
    A_STATUS      status = A_OK;
    HTC_PACKET   *pIOPacket;

    /* this is called in an ASYNC only context, we may NOT block, sleep or call any apis that can
     * cause us to switch contexts */

   AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("+DevCheckPendingRecvMsgsAsync: (dev: 0x%lX)\n", (unsigned long)pDev));

   do {

        if (HIF_DEVICE_IRQ_SYNC_ONLY == pDev->HifIRQProcessingMode) {
                /* break the async processing chain right here, no need to continue.
                 * The DevDsrHandler() will handle things in a loop when things are driven
                 * synchronously  */
            break;
        }

            /* an optimization to bypass reading the IRQ status registers unecessarily which can re-wake
             * the target, if upper layers determine that we are in a low-throughput mode, we can
             * rely on taking another interrupt rather than re-checking the status registers which can
             * re-wake the target */
        if (pDev->RecheckIRQStatusCnt == 0) {
            AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("Bypassing IRQ Status re-check, re-acking HIF interrupts\n"));
                /* ack interrupt */
            HIFAckInterrupt(pDev->HIFDevice);
            break;
        }

            /* first allocate one of our HTC packets we created for async I/O
             * we reuse HTC packet definitions so that we can use the completion mechanism
             * in DevRWCompletionHandler() */
        pIOPacket = AR6KAllocIOPacket(pDev);

        if (NULL == pIOPacket) {
                /* there should be only 1 asynchronous request out at a time to read these registers
                 * so this should actually never happen */
            status = A_NO_MEMORY;
            A_ASSERT(FALSE);
            break;
        }

            /* stick in our completion routine when the I/O operation completes */
        pIOPacket->Completion = DevGetEventAsyncHandler;
        pIOPacket->pContext = pDev;

        if (pDev->GetPendingEventsFunc) {
                /* HIF layer has it's own mechanism, pass the IO to it.. */
            status = pDev->GetPendingEventsFunc(pDev->HIFDevice,
                                                (HIF_PENDING_EVENTS_INFO *)pIOPacket->pBuffer,
                                                pIOPacket);

        } else {
                /* standard way, read the interrupt register table asynchronously again */
            status = HIFReadWrite(pDev->HIFDevice,
                                  HOST_INT_STATUS_ADDRESS,
                                  pIOPacket->pBuffer,
                                  AR6K_IRQ_PROC_REGS_SIZE,
                                  HIF_RD_ASYNC_BYTE_INC,
                                  pIOPacket);
        }

        AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,(" Async IO issued to get interrupt status...\n"));
   } while (FALSE);

   AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("-DevCheckPendingRecvMsgsAsync \n"));

   return status;
}

void DevAsyncIrqProcessComplete(AR6K_DEVICE *pDev)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("DevAsyncIrqProcessComplete - forcing HIF IRQ ACK \n"));
    HIFAckInterrupt(pDev->HIFDevice);
}

/* process pending interrupts synchronously */
static A_STATUS ProcessPendingIRQs(AR6K_DEVICE *pDev, A_BOOL *pDone, A_BOOL *pASyncProcessing)
{
    A_STATUS    status = A_OK;
    A_UINT8     host_int_status = 0;
    A_UINT32    lookAhead = 0;

    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("+ProcessPendingIRQs: (dev: 0x%lX)\n", (unsigned long)pDev));

    /*** NOTE: the HIF implementation guarantees that the context of this call allows
     *         us to perform SYNCHRONOUS I/O, that is we can block, sleep or call any API that
     *         can block or switch thread/task ontexts.
     *         This is a fully schedulable context.
     * */
    do {

            if (pDev->IrqEnableRegisters.int_status_enable == 0) {
                /* interrupt enables have been cleared, do not try to process any pending interrupts that
                 * may result in more bus transactions.  The target may be unresponsive at this
                 * point. */
                 break;
            }

            if (pDev->GetPendingEventsFunc != NULL) {
                HIF_PENDING_EVENTS_INFO events;

#ifdef THREAD_X
            events.Polling= 0;
#endif
                /* the HIF layer uses a special mechanism to get events
                 * get this synchronously  */
            status = pDev->GetPendingEventsFunc(pDev->HIFDevice,
                                                &events,
                                                NULL);

            if (A_FAILED(status)) {
                break;
            }

            if (events.Events & HIF_RECV_MSG_AVAIL) {
                lookAhead = events.LookAhead;
                if (0 == lookAhead) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,(" ProcessPendingIRQs1 lookAhead is zero! \n"));
                }
            }

            if (!(events.Events & HIF_OTHER_EVENTS) ||
                !(pDev->IrqEnableRegisters.int_status_enable & OTHER_INTS_ENABLED)) {
                    /* no need to read the register table, no other interesting interrupts.
                     * Some interfaces (like SPI) can shadow interrupt sources without
                     * requiring the host to do a full table read */
                break;
            }

            /* otherwise fall through and read the register table */
        }

        /*
         * Read the first 28 bytes of the HTC register table. This will yield us
         * the value of different int status registers and the lookahead
         * registers.
         *    length = sizeof(int_status) + sizeof(cpu_int_status) +
         *             sizeof(error_int_status) + sizeof(counter_int_status) +
         *             sizeof(mbox_frame) + sizeof(rx_lookahead_valid) +
         *             sizeof(hole) +  sizeof(rx_lookahead) +
         *             sizeof(int_status_enable) + sizeof(cpu_int_status_enable) +
         *             sizeof(error_status_enable) +
         *             sizeof(counter_int_status_enable);
         *
        */
#ifdef CONFIG_MMC_SDHCI_S3C
            pDev->IrqProcRegisters.host_int_status = 0;
            pDev->IrqProcRegisters.rx_lookahead_valid = 0;
            pDev->IrqProcRegisters.host_int_status2 = 0;
            pDev->IrqProcRegisters.rx_lookahead[0] = 0;
            pDev->IrqProcRegisters.rx_lookahead[1] = 0xaaa5555;
#endif /* CONFIG_MMC_SDHCI_S3C */
        status = HIFReadWrite(pDev->HIFDevice,
                              HOST_INT_STATUS_ADDRESS,
                              (A_UINT8 *)&pDev->IrqProcRegisters,
                              AR6K_IRQ_PROC_REGS_SIZE,
                              HIF_RD_SYNC_BYTE_INC,
                              NULL);

        if (A_FAILED(status)) {
            break;
        }

#ifdef ATH_DEBUG_MODULE
        if (AR_DEBUG_LVL_CHECK(ATH_DEBUG_IRQ)) {
            DevDumpRegisters(pDev,
                             &pDev->IrqProcRegisters,
                             &pDev->IrqEnableRegisters);
        }
#endif

            /* Update only those registers that are enabled */
        host_int_status = pDev->IrqProcRegisters.host_int_status &
                          pDev->IrqEnableRegisters.int_status_enable;

        if (NULL == pDev->GetPendingEventsFunc) {
                /* only look at mailbox status if the HIF layer did not provide this function,
                 * on some HIF interfaces reading the RX lookahead is not valid to do */
            if (host_int_status & (1 << HTC_MAILBOX)) {
                    /* mask out pending mailbox value, we use "lookAhead" as the real flag for
                     * mailbox processing below */
                host_int_status &= ~(1 << HTC_MAILBOX);
                if (pDev->IrqProcRegisters.rx_lookahead_valid & (1 << HTC_MAILBOX)) {
                        /* mailbox has a message and the look ahead is valid */
                    lookAhead = pDev->IrqProcRegisters.rx_lookahead[HTC_MAILBOX];
                    if (0 == lookAhead) {
                        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,(" ProcessPendingIRQs2, lookAhead is zero! \n"));
                    }
                }
            }
        } else {
                /* not valid to check if the HIF has another mechanism for reading mailbox pending status*/
            host_int_status &= ~(1 << HTC_MAILBOX);
        }

        if (pDev->GMboxEnabled) {
                /*call GMBOX layer to process any interrupts of interest */
            status = DevCheckGMboxInterrupts(pDev);
        }

    } while (FALSE);


    do {

            /* did the interrupt status fetches succeed? */
        if (A_FAILED(status)) {
            break;
        }

        if ((0 == host_int_status) && (0 == lookAhead)) {
                /* nothing to process, the caller can use this to break out of a loop */
            *pDone = TRUE;
            break;
        }

        if (lookAhead != 0) {
            int fetched = 0;

            AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("Pending mailbox message, LookAhead: 0x%X\n",lookAhead));
                /* Mailbox Interrupt, the HTC layer may issue async requests to empty the
                 * mailbox...
                 * When emptying the recv mailbox we use the async handler above called from the
                 * completion routine of the callers read request. This can improve performance
                 * by reducing context switching when we rapidly pull packets */
            status = pDev->MessagePendingCallback(pDev->HTCContext, &lookAhead, 1, pASyncProcessing, &fetched);
            if (A_FAILED(status)) {
                break;
            }

            if (!fetched) {
                    /* HTC could not pull any messages out due to lack of resources */
                    /* force DSR handler to ack the interrupt */
                *pASyncProcessing = FALSE;
                pDev->RecheckIRQStatusCnt = 0;
            }
        }

            /* now handle the rest of them */
        AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,
                            (" Valid interrupt source(s) for OTHER interrupts: 0x%x\n",
                            host_int_status));

        if (HOST_INT_STATUS_CPU_GET(host_int_status)) {
                /* CPU Interrupt */
            status = DevServiceCPUInterrupt(pDev);
            if (A_FAILED(status)){
                break;
            }
        }

        if (HOST_INT_STATUS_ERROR_GET(host_int_status)) {
                /* Error Interrupt */
            status = DevServiceErrorInterrupt(pDev);
            if (A_FAILED(status)){
                break;
            }
        }

        if (HOST_INT_STATUS_COUNTER_GET(host_int_status)) {
                /* Counter Interrupt */
            status = DevServiceCounterInterrupt(pDev);
            if (A_FAILED(status)){
                break;
            }
        }

    } while (FALSE);

        /* an optimization to bypass reading the IRQ status registers unecessarily which can re-wake
         * the target, if upper layers determine that we are in a low-throughput mode, we can
         * rely on taking another interrupt rather than re-checking the status registers which can
         * re-wake the target.
         *
         * NOTE : for host interfaces that use the special GetPendingEventsFunc, this optimization cannot
         * be used due to possible side-effects.  For example, SPI requires the host to drain all
         * messages from the mailbox before exiting the ISR routine. */
    if (!(*pASyncProcessing) && (pDev->RecheckIRQStatusCnt == 0) && (pDev->GetPendingEventsFunc == NULL)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("Bypassing IRQ Status re-check, forcing done \n"));
        *pDone = TRUE;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("-ProcessPendingIRQs: (done:%d, async:%d) status=%d \n",
                *pDone, *pASyncProcessing, status));

    return status;
}


/* Synchronousinterrupt handler, this handler kicks off all interrupt processing.*/
A_STATUS DevDsrHandler(void *context)
{
    AR6K_DEVICE *pDev = (AR6K_DEVICE *)context;
    A_STATUS    status = A_OK;
    A_BOOL      done = FALSE;
    A_BOOL      asyncProc = FALSE;

    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("+DevDsrHandler: (dev: 0x%lX)\n", (unsigned long)pDev));

        /* reset the recv counter that tracks when we need to yield from the DSR */
    pDev->CurrentDSRRecvCount = 0;
        /* reset counter used to flag a re-scan of IRQ status registers on the target */
    pDev->RecheckIRQStatusCnt = 0;

    while (!done) {
        status = ProcessPendingIRQs(pDev, &done, &asyncProc);
        if (A_FAILED(status)) {
            break;
        }

        if (HIF_DEVICE_IRQ_SYNC_ONLY == pDev->HifIRQProcessingMode) {
            /* the HIF layer does not allow async IRQ processing, override the asyncProc flag */
            asyncProc = FALSE;
            /* this will cause us to re-enter ProcessPendingIRQ() and re-read interrupt status registers.
             * this has a nice side effect of blocking us until all async read requests are completed.
             * This behavior is required on some HIF implementations that do not allow ASYNC
             * processing in interrupt handlers (like Windows CE) */

            if (pDev->DSRCanYield && DEV_CHECK_RECV_YIELD(pDev)) {
                /* ProcessPendingIRQs() pulled enough recv messages to satisfy the yield count, stop
                 * checking for more messages and return */
                break;
            }
        }

        if (asyncProc) {
                /* the function performed some async I/O for performance, we
                   need to exit the ISR immediately, the check below will prevent the interrupt from being
                   Ack'd while we handle it asynchronously */
            break;
        }

    }

    if (A_SUCCESS(status) && !asyncProc) {
            /* Ack the interrupt only if :
             *  1. we did not get any errors in processing interrupts
             *  2. there are no outstanding async processing requests */
        if (pDev->DSRCanYield) {
                /* if the DSR can yield do not ACK the interrupt, there could be more pending messages.
                 * The HIF layer must ACK the interrupt on behalf of HTC */
            AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,(" Yield in effect (cur RX count: %d) \n", pDev->CurrentDSRRecvCount));
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,(" Acking interrupt from DevDsrHandler \n"));
            HIFAckInterrupt(pDev->HIFDevice);
        }
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_IRQ,("-DevDsrHandler \n"));
    return status;
}

#ifdef ATH_DEBUG_MODULE
void DumpAR6KDevState(AR6K_DEVICE *pDev)
{
    A_STATUS                    status;
    AR6K_IRQ_ENABLE_REGISTERS   regs;
    AR6K_IRQ_PROC_REGISTERS     procRegs;

    LOCK_AR6K(pDev);
        /* copy into our temp area */
    A_MEMCPY(&regs,&pDev->IrqEnableRegisters,AR6K_IRQ_ENABLE_REGS_SIZE);
    UNLOCK_AR6K(pDev);

        /* load the register table from the device */
    status = HIFReadWrite(pDev->HIFDevice,
                          HOST_INT_STATUS_ADDRESS,
                          (A_UINT8 *)&procRegs,
                          AR6K_IRQ_PROC_REGS_SIZE,
                          HIF_RD_SYNC_BYTE_INC,
                          NULL);

    if (A_FAILED(status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
            ("DumpAR6KDevState : Failed to read register table (%d) \n",status));
        return;
    }

    DevDumpRegisters(pDev,&procRegs,&regs);

    if (pDev->GMboxInfo.pStateDumpCallback != NULL) {
        pDev->GMboxInfo.pStateDumpCallback(pDev->GMboxInfo.pProtocolContext);
    }

        /* dump any bus state at the HIF layer */
    HIFConfigureDevice(pDev->HIFDevice,HIF_DEVICE_DEBUG_BUS_STATE,NULL,0);

}
#endif


