/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/*
 * ***************************************************************************
 *
 *  FILE:     csr_wifi_hip_send.c
 *
 *  PURPOSE:
 *      Code for adding a signal request to the from-host queue.
 *      When the driver bottom-half is run, it will take requests from the
 *      queue and pass them to the UniFi.
 *
 * ***************************************************************************
 */
#include "csr_wifi_hip_unifi.h"
#include "csr_wifi_hip_conversions.h"
#include "csr_wifi_hip_sigs.h"
#include "csr_wifi_hip_card.h"

unifi_TrafficQueue unifi_frame_priority_to_queue(CSR_PRIORITY priority)
{
    switch (priority)
    {
        case CSR_QOS_UP0:
        case CSR_QOS_UP3:
            return UNIFI_TRAFFIC_Q_BE;
        case CSR_QOS_UP1:
        case CSR_QOS_UP2:
            return UNIFI_TRAFFIC_Q_BK;
        case CSR_QOS_UP4:
        case CSR_QOS_UP5:
            return UNIFI_TRAFFIC_Q_VI;
        case CSR_QOS_UP6:
        case CSR_QOS_UP7:
        case CSR_MANAGEMENT:
            return UNIFI_TRAFFIC_Q_VO;
        default:
            return UNIFI_TRAFFIC_Q_BE;
    }
}


CSR_PRIORITY unifi_get_default_downgrade_priority(unifi_TrafficQueue queue)
{
    switch (queue)
    {
        case UNIFI_TRAFFIC_Q_BE:
            return CSR_QOS_UP0;
        case UNIFI_TRAFFIC_Q_BK:
            return CSR_QOS_UP1;
        case UNIFI_TRAFFIC_Q_VI:
            return CSR_QOS_UP5;
        case UNIFI_TRAFFIC_Q_VO:
            return CSR_QOS_UP6;
        default:
            return CSR_QOS_UP0;
    }
}


/*
 * ---------------------------------------------------------------------------
 *  send_signal
 *
 *      This function queues a signal for sending to UniFi.  It first checks
 *      that there is space on the fh_signal_queue for another entry, then
 *      claims any bulk data slots required and copies data into them. Then
 *      increments the fh_signal_queue write count.
 *
 *      The fh_signal_queue is later processed by the driver bottom half
 *      (in unifi_bh()).
 *
 *      This function call unifi_pause_xmit() to pause the flow of data plane
 *      packets when:
 *        - the fh_signal_queue ring buffer is full
 *        - there are less than UNIFI_MAX_DATA_REFERENCES (2) bulk data
 *          slots available.
 *
 *  Arguments:
 *      card            Pointer to card context structure
 *      sigptr          Pointer to the signal to write to UniFi.
 *      siglen          Number of bytes pointer to by sigptr.
 *      bulkdata        Array of pointers to an associated bulk data.
 *      sigq            To which from-host queue to add the signal.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success
 *      CSR_WIFI_HIP_RESULT_NO_SPACE if there were insufficient data slots or
 *                              no free signal queue entry
 *
 * Notes:
 *      Calls unifi_pause_xmit() when the last slots are used.
 * ---------------------------------------------------------------------------
 */
static CsrResult send_signal(card_t *card, const u8 *sigptr, u32 siglen,
                             const bulk_data_param_t *bulkdata,
                             q_t *sigq, u32 priority_q, u32 run_bh)
{
    u16 i, data_slot_size;
    card_signal_t *csptr;
    s16 qe;
    CsrResult r;
    s16 debug_print = 0;

    data_slot_size = CardGetDataSlotSize(card);

    /* Check that the fh_data_queue has a free slot */
    if (!CSR_WIFI_HIP_Q_SLOTS_FREE(sigq))
    {
        unifi_trace(card->ospriv, UDBG3, "send_signal: %s full\n", sigq->name);

        return CSR_WIFI_HIP_RESULT_NO_SPACE;
    }

    /*
     * Now add the signal to the From Host signal queue
     */
    /* Get next slot on queue */
    qe = CSR_WIFI_HIP_Q_NEXT_W_SLOT(sigq);
    csptr = CSR_WIFI_HIP_Q_SLOT_DATA(sigq, qe);

    /* Make up the card_signal struct */
    csptr->signal_length = (u16)siglen;
    CsrMemCpy((void *)csptr->sigbuf, (void *)sigptr, siglen);

    for (i = 0; i < UNIFI_MAX_DATA_REFERENCES; ++i)
    {
        if ((bulkdata != NULL) && (bulkdata->d[i].data_length != 0))
        {
            u32 datalen = bulkdata->d[i].data_length;

            /* Make sure data will fit in a bulk data slot */
            if (bulkdata->d[i].os_data_ptr == NULL)
            {
                unifi_error(card->ospriv, "send_signal - NULL bulkdata[%d]\n", i);
                debug_print++;
                csptr->bulkdata[i].data_length = 0;
            }
            else
            {
                if (datalen > data_slot_size)
                {
                    unifi_error(card->ospriv,
                                "send_signal - Invalid data length %u (@%p), "
                                "truncating\n",
                                datalen, bulkdata->d[i].os_data_ptr);
                    datalen = data_slot_size;
                    debug_print++;
                }
                /* Store the bulk data info in the soft queue. */
                csptr->bulkdata[i].os_data_ptr = (u8 *)bulkdata->d[i].os_data_ptr;
                csptr->bulkdata[i].os_net_buf_ptr = (u8 *)bulkdata->d[i].os_net_buf_ptr;
                csptr->bulkdata[i].net_buf_length = bulkdata->d[i].net_buf_length;
                csptr->bulkdata[i].data_length = datalen;
            }
        }
        else
        {
            UNIFI_INIT_BULK_DATA(&csptr->bulkdata[i]);
        }
    }

    if (debug_print)
    {
        const u8 *sig = sigptr;

        unifi_error(card->ospriv, "Signal(%d): %02x %02x %02x %02x %02x %02x %02x %02x"
                    " %02x %02x %02x %02x %02x %02x %02x %02x\n",
                    siglen,
                    sig[0], sig[1], sig[2], sig[3],
                    sig[4], sig[5], sig[6], sig[7],
                    sig[8], sig[9], sig[10], sig[11],
                    sig[12], sig[13], sig[14], sig[15]);
        unifi_error(card->ospriv, "Bulkdata pointer %p(%d), %p(%d)\n",
                    bulkdata != NULL?bulkdata->d[0].os_data_ptr : NULL,
                    bulkdata != NULL?bulkdata->d[0].data_length : 0,
                    bulkdata != NULL?bulkdata->d[1].os_data_ptr : NULL,
                    bulkdata != NULL?bulkdata->d[1].data_length : 0);
    }

    /* Advance the written count to say there is a new entry */
    CSR_WIFI_HIP_Q_INC_W(sigq);

    /*
     * Set the flag to say reason for waking was a host request.
     * Then ask the OS layer to run the unifi_bh.
     */
    if (run_bh == 1)
    {
        card->bh_reason_host = 1;
        r = unifi_run_bh(card->ospriv);
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "failed to run bh.\n");
            card->bh_reason_host = 0;

            /*
             * The bulk data buffer will be freed by the caller.
             * We need to invalidate the description of the bulk data in our
             * soft queue, to prevent the core freeing the bulk data again later.
             */
            for (i = 0; i < UNIFI_MAX_DATA_REFERENCES; ++i)
            {
                if (csptr->bulkdata[i].data_length != 0)
                {
                    csptr->bulkdata[i].os_data_ptr = csptr->bulkdata[i].os_net_buf_ptr = NULL;
                    csptr->bulkdata[i].net_buf_length = csptr->bulkdata[i].data_length = 0;
                }
            }
            return r;
        }
    }
    else
    {
        unifi_error(card->ospriv, "run_bh=%d, bh not called.\n", run_bh);
    }

    /*
     * Have we used up all the fh signal list entries?
     */
    if (CSR_WIFI_HIP_Q_SLOTS_FREE(sigq) == 0)
    {
        /* We have filled the queue, so stop the upper layer. The command queue
         * is an exception, as suspending due to that being full could delay
         * resume/retry until new commands or data are received.
         */
        if (sigq != &card->fh_command_queue)
        {
            /*
             * Must call unifi_pause_xmit() *before* setting the paused flag.
             * (the unifi_pause_xmit call should not be after setting the flag because of the possibility of being interrupted
             * by the bh thread between our setting the flag and the call to unifi_pause_xmit()
             * If bh thread then cleared the flag, we would end up paused, but without the flag set)
             * Instead, setting it afterwards means that if this thread is interrupted by the bh thread
             * the pause flag is still guaranteed to end up set
             * However the potential deadlock now is that if bh thread emptied the queue and cleared the flag before this thread's
             * call to unifi_pause_xmit(), then bh thread may not run again because it will be waiting for
             * a packet to appear in the queue but nothing ever will because xmit is paused.
             * So we will end up with the queue paused, and the flag set to say it is paused, but bh never runs to unpause it.
             * (Note even this bad situation would not persist long in practice, because something else (eg rx, or tx in different queue)
             * is likely to wake bh thread quite soon)
             * But to avoid this deadlock completely, after setting the flag we check that there is something left in the queue.
             * If there is, we know that bh thread has not emptied the queue yet.
             * Since bh thread checks to unpause the queue *after* taking packets from the queue, we know that it is still going to make at
             * least one more check to see whether it needs to unpause the queue.  So all is well.
             * If there are no packets in the queue, then the deadlock described above might happen.  To make sure it does not, we
             * unpause the queue here. A possible side effect is that unifi_restart_xmit() may (rarely) be called for second time
             *  unnecessarily, which is harmless
             */

#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_DATA_PLANE_PROFILE)
            unifi_debug_log_to_buf("P");
#endif
            unifi_pause_xmit(card->ospriv, (unifi_TrafficQueue)priority_q);
            card_tx_q_pause(card, priority_q);
            if (CSR_WIFI_HIP_Q_SLOTS_USED(sigq) == 0)
            {
                card_tx_q_unpause(card, priority_q);
                unifi_restart_xmit(card->ospriv, (unifi_TrafficQueue) priority_q);
            }
        }
        else
        {
            unifi_warning(card->ospriv,
                          "send_signal: fh_cmd_q full, not pausing (run_bh=%d)\n",
                          run_bh);
        }
    }

    func_exit();

    return CSR_RESULT_SUCCESS;
} /*  send_signal() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_send_signal
 *
 *    Invokes send_signal() to queue a signal in the command or traffic queue
 *    If sigptr pointer is NULL, it pokes the bh to check if UniFi is responsive.
 *
 *  Arguments:
 *      card        Pointer to card context struct
 *      sigptr      Pointer to signal from card.
 *      siglen      Size of the signal
 *      bulkdata    Pointer to the bulk data of the signal
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success
 *      CSR_WIFI_HIP_RESULT_NO_SPACE if there were insufficient data slots or no free signal queue entry
 *
 *  Notes:
 *      unifi_send_signal() is used to queue signals, created by the driver,
 *      to the device. Signals are constructed using the UniFi packed structures.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_send_signal(card_t *card, const u8 *sigptr, u32 siglen,
                            const bulk_data_param_t *bulkdata)
{
    q_t *sig_soft_q;
    u16 signal_id;
    CsrResult r;
    u32 run_bh;
    u32 priority_q;

    /* A NULL signal pointer is a request to check if UniFi is responsive */
    if (sigptr == NULL)
    {
        card->bh_reason_host = 1;
        return unifi_run_bh(card->ospriv);
    }

    priority_q = 0;
    run_bh = 1;
    signal_id = GET_SIGNAL_ID(sigptr);
    /*
     * If the signal is a CSR_MA_PACKET_REQUEST ,
     * we send it using the traffic soft queue. Else we use the command soft queue.
     */
    if (signal_id == CSR_MA_PACKET_REQUEST_ID)
    {
        u16 frame_priority;

        if (card->periodic_wake_mode == UNIFI_PERIODIC_WAKE_HOST_ENABLED)
        {
            run_bh = 0;
        }

#if defined (CSR_WIFI_HIP_DEBUG_OFFLINE) && defined (CSR_WIFI_HIP_DATA_PLANE_PROFILE)
        unifi_debug_log_to_buf("D");
#endif
        /* Sanity check: MA-PACKET.req must have a valid bulk data */
        if ((bulkdata->d[0].data_length == 0) || (bulkdata->d[0].os_data_ptr == NULL))
        {
            unifi_error(card->ospriv, "MA-PACKET.req with empty bulk data (%d bytes in %p)\n",
                        bulkdata->d[0].data_length, bulkdata->d[0].os_data_ptr);
            dump((void *)sigptr, siglen);
            return CSR_RESULT_FAILURE;
        }

        /* Map the frame priority to a traffic queue index. */
        frame_priority = GET_PACKED_MA_PACKET_REQUEST_FRAME_PRIORITY(sigptr);
        priority_q = unifi_frame_priority_to_queue((CSR_PRIORITY)frame_priority);

        sig_soft_q = &card->fh_traffic_queue[priority_q];
    }
    else
    {
        sig_soft_q = &card->fh_command_queue;
    }

    r = send_signal(card, sigptr, siglen, bulkdata, sig_soft_q, priority_q, run_bh);
    /* On error, the caller must free or requeue bulkdata buffers */

    return r;
} /* unifi_send_signal() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_send_resources_available
 *
 *      Examines whether there is available space to queue
 *      a signal in the command or traffic queue
 *
 *  Arguments:
 *      card        Pointer to card context struct
 *      sigptr      Pointer to signal.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS if resources available
 *      CSR_WIFI_HIP_RESULT_NO_SPACE if there was no free signal queue entry
 *
 *  Notes:
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_send_resources_available(card_t *card, const u8 *sigptr)
{
    q_t *sig_soft_q;
    u16 signal_id = GET_SIGNAL_ID(sigptr);

    /*
     * If the signal is a CSR_MA_PACKET_REQUEST ,
     * we send it using the traffic soft queue. Else we use the command soft queue.
     */
    if (signal_id == CSR_MA_PACKET_REQUEST_ID)
    {
        u16 frame_priority;
        u32 priority_q;

        /* Map the frame priority to a traffic queue index. */
        frame_priority = GET_PACKED_MA_PACKET_REQUEST_FRAME_PRIORITY(sigptr);
        priority_q = unifi_frame_priority_to_queue((CSR_PRIORITY)frame_priority);

        sig_soft_q = &card->fh_traffic_queue[priority_q];
    }
    else
    {
        sig_soft_q = &card->fh_command_queue;
    }

    /* Check that the fh_data_queue has a free slot */
    if (!CSR_WIFI_HIP_Q_SLOTS_FREE(sig_soft_q))
    {
        unifi_notice(card->ospriv, "unifi_send_resources_available: %s full\n",
                     sig_soft_q->name);
        return CSR_WIFI_HIP_RESULT_NO_SPACE;
    }

    return CSR_RESULT_SUCCESS;
} /* unifi_send_resources_available() */


