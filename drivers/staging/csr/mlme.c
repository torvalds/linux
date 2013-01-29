/*
 * ---------------------------------------------------------------------------
 * FILE:     mlme.c
 *
 * PURPOSE:
 *      This file provides functions to send MLME requests to the UniFi.
 *
 * Copyright (C) 2007-2008 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ---------------------------------------------------------------------------
 */
#include "csr_wifi_hip_unifi.h"
#include "unifi_priv.h"

/*
 * ---------------------------------------------------------------------------
 * unifi_mlme_wait_for_reply
 *
 *      Wait for a reply after sending a signal.
 *
 * Arguments:
 *      priv            Pointer to device private context struct
 *      ul_client       Pointer to linux client
 *      sig_reply_id    ID of the expected reply (defined in sigs.h).
 *      timeout         timeout in ms
 *
 * Returns:
 *      0 on success, -ve POSIX code on error.
 *
 * Notes:
 *      This function waits for a specific (sig_reply_id) signal from UniFi.
 *      It also match the sequence number of the received (cfm) signal, with
 *      the latest sequence number of the signal (req) we have sent.
 *      These two number match be equal.
 *      Should only be used for waiting xxx.cfm signals and only after
 *      we have sent the matching xxx.req signal to UniFi.
 *      If no response is received within the expected time (timeout), we assume
 *      that the UniFi is busy and return an error.
 *      If the wait is aborted by a kernel signal arriving, we stop waiting.
 *      If a response from UniFi is not what we expected, we discard it and
 *      wait again. This could be a response from an aborted request. If we
 *      see several bad responses we assume we have lost synchronisation with
 *      UniFi.
 * ---------------------------------------------------------------------------
 */
static int
unifi_mlme_wait_for_reply(unifi_priv_t *priv, ul_client_t *pcli, int sig_reply_id, int timeout)
{
    int retries = 0;
    long r;
    long t = timeout;
    unsigned int sent_seq_no;

    /* Convert t in ms to jiffies */
    t = msecs_to_jiffies(t);

    do {
        /* Wait for the confirm or timeout. */
        r = wait_event_interruptible_timeout(pcli->udi_wq,
                                             (pcli->wake_up_wq_id) || (priv->io_aborted == 1),
                                             t);
        /* Check for general i/o error */
        if (priv->io_aborted) {
            unifi_error(priv, "MLME operation aborted\n");
            return -EIO;
        }

        /*
         * If r=0 the request has timed-out.
         * If r>0 the request has completed successfully.
         * If r=-ERESTARTSYS an event (kill signal) has interrupted the wait_event.
         */
        if ((r == 0) && (pcli->wake_up_wq_id == 0)) {
            unifi_error(priv, "mlme_wait: timed-out waiting for 0x%.4X, after %lu msec.\n",
                        sig_reply_id,  jiffies_to_msecs(t));
            pcli->wake_up_wq_id = 0;
            return -ETIMEDOUT;
        } else if (r == -ERESTARTSYS) {
            unifi_error(priv, "mlme_wait: waiting for 0x%.4X was aborted.\n", sig_reply_id);
            pcli->wake_up_wq_id = 0;
            return -EINTR;
        } else {
            /* Get the sequence number of the signal that we previously set. */
            if (pcli->seq_no != 0) {
                sent_seq_no = pcli->seq_no - 1;
            } else {
                sent_seq_no = 0x0F;
            }

            unifi_trace(priv, UDBG5, "Received 0x%.4X, seq: (r:%d, s:%d)\n",
                        pcli->wake_up_wq_id,
                        pcli->wake_seq_no, sent_seq_no);

            /* The two sequence ids must match. */
            if (pcli->wake_seq_no == sent_seq_no) {
                /* and the signal ids must match. */
                if (sig_reply_id == pcli->wake_up_wq_id) {
                    /* Found the expected signal */
                    break;
                } else {
                    /* This should never happen ... */
                    unifi_error(priv, "mlme_wait: mismatching signal id (0x%.4X - exp 0x%.4X) (seq %d)\n",
                                pcli->wake_up_wq_id,
                                sig_reply_id,
                                pcli->wake_seq_no);
                    pcli->wake_up_wq_id = 0;
                    return -EIO;
                }
            }
            /* Wait for the next signal. */
            pcli->wake_up_wq_id = 0;

            retries ++;
            if (retries >= 3) {
                unifi_error(priv, "mlme_wait: confirm wait retries exhausted (0x%.4X - exp 0x%.4X)\n",
                            pcli->wake_up_wq_id,
                            sig_reply_id);
                pcli->wake_up_wq_id = 0;
                return -EIO;
            }
        }
    } while (1);

    pcli->wake_up_wq_id = 0;

    return 0;
} /* unifi_mlme_wait_for_reply() */


/*
 * ---------------------------------------------------------------------------
 * unifi_mlme_blocking_request
 *
 *      Send a MLME request signal to UniFi.
 *
 * Arguments:
 *      priv            Pointer to device private context struct
 *      pcli            Pointer to context of calling process
 *      sig             Pointer to the signal to send
 *      data_ptrs       Pointer to the bulk data of the signal
 *      timeout         The request's timeout.
 *
 * Returns:
 *      0 on success, 802.11 result code on error.
 * ---------------------------------------------------------------------------
 */
int
unifi_mlme_blocking_request(unifi_priv_t *priv, ul_client_t *pcli,
                            CSR_SIGNAL *sig, bulk_data_param_t *data_ptrs,
                            int timeout)
{
    int r;

    if (sig->SignalPrimitiveHeader.SignalId == 0) {
        unifi_error(priv, "unifi_mlme_blocking_request: Invalid Signal Id (0x%x)\n",
                    sig->SignalPrimitiveHeader.SignalId);
        return -EINVAL;
    }

    down(&priv->mlme_blocking_mutex);

    sig->SignalPrimitiveHeader.ReceiverProcessId = 0;
    sig->SignalPrimitiveHeader.SenderProcessId = pcli->sender_id | pcli->seq_no;

    unifi_trace(priv, UDBG2, "Send client=%d, S:0x%04X, sig 0x%.4X\n",
                pcli->client_id,
                sig->SignalPrimitiveHeader.SenderProcessId,
                sig->SignalPrimitiveHeader.SignalId);
    /* Send the signal to UniFi */
    r = ul_send_signal_unpacked(priv, sig, data_ptrs);
    if (r) {
        up(&priv->mlme_blocking_mutex);
        unifi_error(priv, "Error queueing MLME REQUEST signal\n");
        return r;
    }

    unifi_trace(priv, UDBG5, "Send 0x%.4X, seq = %d\n",
                sig->SignalPrimitiveHeader.SignalId, pcli->seq_no);

    /*
     * Advance the sequence number of the last sent signal, only
     * if the signal has been successfully set.
     */
    pcli->seq_no++;
    if (pcli->seq_no > 0x0F) {
        pcli->seq_no = 0;
    }

    r = unifi_mlme_wait_for_reply(priv, pcli, (sig->SignalPrimitiveHeader.SignalId + 1), timeout);
    up(&priv->mlme_blocking_mutex);

    if (r) {
        unifi_error(priv, "Error waiting for MLME CONFIRM signal\n");
        return r;
    }

    return 0;
} /* unifi_mlme_blocking_request() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_mlme_copy_reply_and_wakeup_client
 *
 *      Copy the reply signal from UniFi to the client's structure
 *      and wake up the waiting client.
 *
 *  Arguments:
 *      None.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
void
unifi_mlme_copy_reply_and_wakeup_client(ul_client_t *pcli,
                                        CSR_SIGNAL *signal, int signal_len,
                                        const bulk_data_param_t *bulkdata)
{
    int i;

    /* Copy the signal to the reply */
    memcpy(pcli->reply_signal, signal, signal_len);

    /* Get the sequence number of the signal that woke us up. */
    pcli->wake_seq_no = pcli->reply_signal->SignalPrimitiveHeader.ReceiverProcessId & 0x0F;

    /* Append any bulk data */
    for (i = 0; i < UNIFI_MAX_DATA_REFERENCES; i++) {
        if (bulkdata->d[i].data_length > 0) {
            if (bulkdata->d[i].os_data_ptr) {
                memcpy(pcli->reply_bulkdata[i]->ptr, bulkdata->d[i].os_data_ptr, bulkdata->d[i].data_length);
                pcli->reply_bulkdata[i]->length = bulkdata->d[i].data_length;
            } else {
                pcli->reply_bulkdata[i]->length = 0;
            }
        }
    }

    /* Wake the requesting MLME function. */
    pcli->wake_up_wq_id = pcli->reply_signal->SignalPrimitiveHeader.SignalId;
    wake_up_interruptible(&pcli->udi_wq);

} /* unifi_mlme_copy_reply_and_wakeup_client() */


/*
 * ---------------------------------------------------------------------------
 *  uf_abort_mlme
 *
 *      Abort any MLME operation in progress.
 *      This is used in the error recovery mechanism.
 *
 *  Arguments:
 *      priv          Pointer to driver context.
 *
 *  Returns:
 *      0 on success.
 * ---------------------------------------------------------------------------
 */
int
uf_abort_mlme(unifi_priv_t *priv)
{
    ul_client_t *ul_cli;

    /* Ensure no MLME functions are waiting on a the mlme_event semaphore. */
    priv->io_aborted = 1;

    ul_cli = priv->netdev_client;
    if (ul_cli) {
        wake_up_interruptible(&ul_cli->udi_wq);
    }

    ul_cli = priv->wext_client;
    if (ul_cli) {
        wake_up_interruptible(&ul_cli->udi_wq);
    }

    return 0;
} /* uf_abort_mlme() */



/*
 * ---------------------------------------------------------------------------
 *
 *      Human-readable decoding of Reason and Result codes.
 *
 * ---------------------------------------------------------------------------
 */

struct mlme_code {
    const char *name;
    int id;
};

static const struct mlme_code Result_codes[] = {
    { "Success",                             0x0000 },
    { "Unspecified Failure",                 0x0001 },
    /* (Reserved)                      0x0002 - 0x0009 */
    { "Refused Capabilities Mismatch",       0x000A },
    /* (Reserved)                          0x000B */
    { "Refused External Reason",             0x000C },
    /* (Reserved)                      0x000D - 0x0010 */
    { "Refused AP Out Of Memory",            0x0011 },
    { "Refused Basic Rates Mismatch",        0x0012 },
    /* (Reserved)                      0x0013 - 0x001F */
    { "Failure",                             0x0020 },
    /* (Reserved)                      0x0021 - 0x0024 */
    { "Refused Reason Unspecified",          0x0025 },
    { "Invalid Parameters",                  0x0026 },
    { "Rejected With Suggested Changes",     0x0027 },
    /* (Reserved)                      0x0028 - 0x002E */
    { "Rejected For Delay Period",           0x002F },
    { "Not Allowed",                         0x0030 },
    { "Not Present",                         0x0031 },
    { "Not QSTA",                            0x0032 },
    /* (Reserved)                      0x0033 - 0x7FFF */
    { "Timeout",                             0x8000 },
    { "Too Many Simultaneous Requests",      0x8001 },
    { "BSS Already Started Or Joined",       0x8002 },
    { "Not Supported",                       0x8003 },
    { "Transmission Failure",                0x8004 },
    { "Refused Not Authenticated",           0x8005 },
    { "Reset Required Before Start",         0x8006 },
    { "LM Info Unavailable",                 0x8007 },
    { NULL, -1 }
};

static const struct mlme_code Reason_codes[] = {
    /* (Reserved)                      0x0000 */
    { "Unspecified Reason",              0x0001 },
    { "Authentication Not Valid",        0x0002 },
    { "Deauthenticated Leave BSS",       0x0003 },
    { "Disassociated Inactivity",        0x0004 },
    { "AP Overload",                     0x0005 },
    { "Class2 Frame Error",              0x0006 },
    { "Class3 Frame Error",              0x0007 },
    { "Disassociated Leave BSS",         0x0008 },
    { "Association Not Authenticated",   0x0009 },
    { "Disassociated Power Capability",  0x000A },
    { "Disassociated Supported Channels", 0x000B },
    /* (Reserved)                      0x000C */
    { "Invalid Information Element",     0x000D },
    { "Michael MIC Failure",             0x000E },
    { "Fourway Handshake Timeout",       0x000F },
    { "Group Key Update Timeout",        0x0010 },
    { "Handshake Element Different",     0x0011 },
    { "Invalid Group Cipher",            0x0012 },
    { "Invalid Pairwise Cipher",         0x0013 },
    { "Invalid AKMP",                    0x0014 },
    { "Unsupported RSN IE Version",      0x0015 },
    { "Invalid RSN IE Capabilities",     0x0016 },
    { "Dot1X Auth Failed",               0x0017 },
    { "Cipher Rejected By Policy",       0x0018 },
    /* (Reserved)                  0x0019 - 0x001F */
    { "QoS Unspecified Reason",          0x0020 },
    { "QoS Insufficient Bandwidth",      0x0021 },
    { "QoS Excessive Not Ack",           0x0022 },
    { "QoS TXOP Limit Exceeded",         0x0023 },
    { "QSTA Leaving",                    0x0024 },
    { "End TS, End DLS, End BA",         0x0025 },
    { "Unknown TS, Unknown DLS, Unknown BA", 0x0026 },
    { "Timeout",                         0x0027 },
    /* (Reserved)                  0x0028 - 0x002C */
    { "STAKey Mismatch",                 0x002D },
    { NULL, -1 }
};


static const char *
lookup_something(const struct mlme_code *n, int id)
{
    for (; n->name; n++) {
        if (n->id == id) {
            return n->name;
        }
    }

    /* not found */
    return NULL;
} /* lookup_something() */


const char *
lookup_result_code(int result)
{
    static char fallback[16];
    const char *str;

    str = lookup_something(Result_codes, result);

    if (str == NULL) {
        snprintf(fallback, 16, "%d", result);
        str = fallback;
    }

    return str;
} /* lookup_result_code() */


/*
 * ---------------------------------------------------------------------------
 *  lookup_reason
 *
 *      Return a description string for a WiFi MLME ReasonCode.
 *
 *  Arguments:
 *      reason          The ReasonCode to interpret.
 *
 *  Returns:
 *      Pointer to description string.
 * ---------------------------------------------------------------------------
 */
const char *
lookup_reason_code(int reason)
{
    static char fallback[16];
    const char *str;

    str = lookup_something(Reason_codes, reason);

    if (str == NULL) {
        snprintf(fallback, 16, "%d", reason);
        str = fallback;
    }

    return str;
} /* lookup_reason_code() */

