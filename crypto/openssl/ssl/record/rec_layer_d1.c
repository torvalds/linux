/*
 * Copyright 2005-2019 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <errno.h>
#include "../ssl_locl.h"
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include "record_locl.h"
#include "../packet_locl.h"
#include "internal/cryptlib.h"

int DTLS_RECORD_LAYER_new(RECORD_LAYER *rl)
{
    DTLS_RECORD_LAYER *d;

    if ((d = OPENSSL_malloc(sizeof(*d))) == NULL) {
        SSLerr(SSL_F_DTLS_RECORD_LAYER_NEW, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    rl->d = d;

    d->unprocessed_rcds.q = pqueue_new();
    d->processed_rcds.q = pqueue_new();
    d->buffered_app_data.q = pqueue_new();

    if (d->unprocessed_rcds.q == NULL || d->processed_rcds.q == NULL
        || d->buffered_app_data.q == NULL) {
        pqueue_free(d->unprocessed_rcds.q);
        pqueue_free(d->processed_rcds.q);
        pqueue_free(d->buffered_app_data.q);
        OPENSSL_free(d);
        rl->d = NULL;
        return 0;
    }

    return 1;
}

void DTLS_RECORD_LAYER_free(RECORD_LAYER *rl)
{
    DTLS_RECORD_LAYER_clear(rl);
    pqueue_free(rl->d->unprocessed_rcds.q);
    pqueue_free(rl->d->processed_rcds.q);
    pqueue_free(rl->d->buffered_app_data.q);
    OPENSSL_free(rl->d);
    rl->d = NULL;
}

void DTLS_RECORD_LAYER_clear(RECORD_LAYER *rl)
{
    DTLS_RECORD_LAYER *d;
    pitem *item = NULL;
    DTLS1_RECORD_DATA *rdata;
    pqueue *unprocessed_rcds;
    pqueue *processed_rcds;
    pqueue *buffered_app_data;

    d = rl->d;

    while ((item = pqueue_pop(d->unprocessed_rcds.q)) != NULL) {
        rdata = (DTLS1_RECORD_DATA *)item->data;
        OPENSSL_free(rdata->rbuf.buf);
        OPENSSL_free(item->data);
        pitem_free(item);
    }

    while ((item = pqueue_pop(d->processed_rcds.q)) != NULL) {
        rdata = (DTLS1_RECORD_DATA *)item->data;
        OPENSSL_free(rdata->rbuf.buf);
        OPENSSL_free(item->data);
        pitem_free(item);
    }

    while ((item = pqueue_pop(d->buffered_app_data.q)) != NULL) {
        rdata = (DTLS1_RECORD_DATA *)item->data;
        OPENSSL_free(rdata->rbuf.buf);
        OPENSSL_free(item->data);
        pitem_free(item);
    }

    unprocessed_rcds = d->unprocessed_rcds.q;
    processed_rcds = d->processed_rcds.q;
    buffered_app_data = d->buffered_app_data.q;
    memset(d, 0, sizeof(*d));
    d->unprocessed_rcds.q = unprocessed_rcds;
    d->processed_rcds.q = processed_rcds;
    d->buffered_app_data.q = buffered_app_data;
}

void DTLS_RECORD_LAYER_set_saved_w_epoch(RECORD_LAYER *rl, unsigned short e)
{
    if (e == rl->d->w_epoch - 1) {
        memcpy(rl->d->curr_write_sequence,
               rl->write_sequence, sizeof(rl->write_sequence));
        memcpy(rl->write_sequence,
               rl->d->last_write_sequence, sizeof(rl->write_sequence));
    } else if (e == rl->d->w_epoch + 1) {
        memcpy(rl->d->last_write_sequence,
               rl->write_sequence, sizeof(unsigned char[8]));
        memcpy(rl->write_sequence,
               rl->d->curr_write_sequence, sizeof(rl->write_sequence));
    }
    rl->d->w_epoch = e;
}

void DTLS_RECORD_LAYER_set_write_sequence(RECORD_LAYER *rl, unsigned char *seq)
{
    memcpy(rl->write_sequence, seq, SEQ_NUM_SIZE);
}

/* copy buffered record into SSL structure */
static int dtls1_copy_record(SSL *s, pitem *item)
{
    DTLS1_RECORD_DATA *rdata;

    rdata = (DTLS1_RECORD_DATA *)item->data;

    SSL3_BUFFER_release(&s->rlayer.rbuf);

    s->rlayer.packet = rdata->packet;
    s->rlayer.packet_length = rdata->packet_length;
    memcpy(&s->rlayer.rbuf, &(rdata->rbuf), sizeof(SSL3_BUFFER));
    memcpy(&s->rlayer.rrec, &(rdata->rrec), sizeof(SSL3_RECORD));

    /* Set proper sequence number for mac calculation */
    memcpy(&(s->rlayer.read_sequence[2]), &(rdata->packet[5]), 6);

    return 1;
}

int dtls1_buffer_record(SSL *s, record_pqueue *queue, unsigned char *priority)
{
    DTLS1_RECORD_DATA *rdata;
    pitem *item;

    /* Limit the size of the queue to prevent DOS attacks */
    if (pqueue_size(queue->q) >= 100)
        return 0;

    rdata = OPENSSL_malloc(sizeof(*rdata));
    item = pitem_new(priority, rdata);
    if (rdata == NULL || item == NULL) {
        OPENSSL_free(rdata);
        pitem_free(item);
        SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DTLS1_BUFFER_RECORD,
                 ERR_R_INTERNAL_ERROR);
        return -1;
    }

    rdata->packet = s->rlayer.packet;
    rdata->packet_length = s->rlayer.packet_length;
    memcpy(&(rdata->rbuf), &s->rlayer.rbuf, sizeof(SSL3_BUFFER));
    memcpy(&(rdata->rrec), &s->rlayer.rrec, sizeof(SSL3_RECORD));

    item->data = rdata;

#ifndef OPENSSL_NO_SCTP
    /* Store bio_dgram_sctp_rcvinfo struct */
    if (BIO_dgram_is_sctp(SSL_get_rbio(s)) &&
        (SSL_get_state(s) == TLS_ST_SR_FINISHED
         || SSL_get_state(s) == TLS_ST_CR_FINISHED)) {
        BIO_ctrl(SSL_get_rbio(s), BIO_CTRL_DGRAM_SCTP_GET_RCVINFO,
                 sizeof(rdata->recordinfo), &rdata->recordinfo);
    }
#endif

    s->rlayer.packet = NULL;
    s->rlayer.packet_length = 0;
    memset(&s->rlayer.rbuf, 0, sizeof(s->rlayer.rbuf));
    memset(&s->rlayer.rrec, 0, sizeof(s->rlayer.rrec));

    if (!ssl3_setup_buffers(s)) {
        /* SSLfatal() already called */
        OPENSSL_free(rdata->rbuf.buf);
        OPENSSL_free(rdata);
        pitem_free(item);
        return -1;
    }

    if (pqueue_insert(queue->q, item) == NULL) {
        /* Must be a duplicate so ignore it */
        OPENSSL_free(rdata->rbuf.buf);
        OPENSSL_free(rdata);
        pitem_free(item);
    }

    return 1;
}

int dtls1_retrieve_buffered_record(SSL *s, record_pqueue *queue)
{
    pitem *item;

    item = pqueue_pop(queue->q);
    if (item) {
        dtls1_copy_record(s, item);

        OPENSSL_free(item->data);
        pitem_free(item);

        return 1;
    }

    return 0;
}

/*
 * retrieve a buffered record that belongs to the new epoch, i.e., not
 * processed yet
 */
#define dtls1_get_unprocessed_record(s) \
                   dtls1_retrieve_buffered_record((s), \
                   &((s)->rlayer.d->unprocessed_rcds))

int dtls1_process_buffered_records(SSL *s)
{
    pitem *item;
    SSL3_BUFFER *rb;
    SSL3_RECORD *rr;
    DTLS1_BITMAP *bitmap;
    unsigned int is_next_epoch;
    int replayok = 1;

    item = pqueue_peek(s->rlayer.d->unprocessed_rcds.q);
    if (item) {
        /* Check if epoch is current. */
        if (s->rlayer.d->unprocessed_rcds.epoch != s->rlayer.d->r_epoch)
            return 1;         /* Nothing to do. */

        rr = RECORD_LAYER_get_rrec(&s->rlayer);

        rb = RECORD_LAYER_get_rbuf(&s->rlayer);

        if (SSL3_BUFFER_get_left(rb) > 0) {
            /*
             * We've still got data from the current packet to read. There could
             * be a record from the new epoch in it - so don't overwrite it
             * with the unprocessed records yet (we'll do it when we've
             * finished reading the current packet).
             */
            return 1;
        }

        /* Process all the records. */
        while (pqueue_peek(s->rlayer.d->unprocessed_rcds.q)) {
            dtls1_get_unprocessed_record(s);
            bitmap = dtls1_get_bitmap(s, rr, &is_next_epoch);
            if (bitmap == NULL) {
                /*
                 * Should not happen. This will only ever be NULL when the
                 * current record is from a different epoch. But that cannot
                 * be the case because we already checked the epoch above
                 */
                 SSLfatal(s, SSL_AD_INTERNAL_ERROR,
                          SSL_F_DTLS1_PROCESS_BUFFERED_RECORDS,
                          ERR_R_INTERNAL_ERROR);
                 return 0;
            }
#ifndef OPENSSL_NO_SCTP
            /* Only do replay check if no SCTP bio */
            if (!BIO_dgram_is_sctp(SSL_get_rbio(s)))
#endif
            {
                /*
                 * Check whether this is a repeat, or aged record. We did this
                 * check once already when we first received the record - but
                 * we might have updated the window since then due to
                 * records we subsequently processed.
                 */
                replayok = dtls1_record_replay_check(s, bitmap);
            }

            if (!replayok || !dtls1_process_record(s, bitmap)) {
                if (ossl_statem_in_error(s)) {
                    /* dtls1_process_record called SSLfatal() */
                    return -1;
                }
                /* dump this record */
                rr->length = 0;
                RECORD_LAYER_reset_packet_length(&s->rlayer);
                continue;
            }

            if (dtls1_buffer_record(s, &(s->rlayer.d->processed_rcds),
                    SSL3_RECORD_get_seq_num(s->rlayer.rrec)) < 0) {
                /* SSLfatal() already called */
                return 0;
            }
        }
    }

    /*
     * sync epoch numbers once all the unprocessed records have been
     * processed
     */
    s->rlayer.d->processed_rcds.epoch = s->rlayer.d->r_epoch;
    s->rlayer.d->unprocessed_rcds.epoch = s->rlayer.d->r_epoch + 1;

    return 1;
}

/*-
 * Return up to 'len' payload bytes received in 'type' records.
 * 'type' is one of the following:
 *
 *   -  SSL3_RT_HANDSHAKE (when ssl3_get_message calls us)
 *   -  SSL3_RT_APPLICATION_DATA (when ssl3_read calls us)
 *   -  0 (during a shutdown, no data has to be returned)
 *
 * If we don't have stored data to work from, read a SSL/TLS record first
 * (possibly multiple records if we still don't have anything to return).
 *
 * This function must handle any surprises the peer may have for us, such as
 * Alert records (e.g. close_notify) or renegotiation requests. ChangeCipherSpec
 * messages are treated as if they were handshake messages *if* the |recd_type|
 * argument is non NULL.
 * Also if record payloads contain fragments too small to process, we store
 * them until there is enough for the respective protocol (the record protocol
 * may use arbitrary fragmentation and even interleaving):
 *     Change cipher spec protocol
 *             just 1 byte needed, no need for keeping anything stored
 *     Alert protocol
 *             2 bytes needed (AlertLevel, AlertDescription)
 *     Handshake protocol
 *             4 bytes needed (HandshakeType, uint24 length) -- we just have
 *             to detect unexpected Client Hello and Hello Request messages
 *             here, anything else is handled by higher layers
 *     Application data protocol
 *             none of our business
 */
int dtls1_read_bytes(SSL *s, int type, int *recvd_type, unsigned char *buf,
                     size_t len, int peek, size_t *readbytes)
{
    int i, j, iret;
    size_t n;
    SSL3_RECORD *rr;
    void (*cb) (const SSL *ssl, int type2, int val) = NULL;

    if (!SSL3_BUFFER_is_initialised(&s->rlayer.rbuf)) {
        /* Not initialized yet */
        if (!ssl3_setup_buffers(s)) {
            /* SSLfatal() already called */
            return -1;
        }
    }

    if ((type && (type != SSL3_RT_APPLICATION_DATA) &&
         (type != SSL3_RT_HANDSHAKE)) ||
        (peek && (type != SSL3_RT_APPLICATION_DATA))) {
        SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DTLS1_READ_BYTES,
                 ERR_R_INTERNAL_ERROR);
        return -1;
    }

    if (!ossl_statem_get_in_handshake(s) && SSL_in_init(s)) {
        /* type == SSL3_RT_APPLICATION_DATA */
        i = s->handshake_func(s);
        /* SSLfatal() already called if appropriate */
        if (i < 0)
            return i;
        if (i == 0)
            return -1;
    }

 start:
    s->rwstate = SSL_NOTHING;

    /*-
     * s->s3->rrec.type         - is the type of record
     * s->s3->rrec.data,    - data
     * s->s3->rrec.off,     - offset into 'data' for next read
     * s->s3->rrec.length,  - number of bytes.
     */
    rr = s->rlayer.rrec;

    /*
     * We are not handshaking and have no data yet, so process data buffered
     * during the last handshake in advance, if any.
     */
    if (SSL_is_init_finished(s) && SSL3_RECORD_get_length(rr) == 0) {
        pitem *item;
        item = pqueue_pop(s->rlayer.d->buffered_app_data.q);
        if (item) {
#ifndef OPENSSL_NO_SCTP
            /* Restore bio_dgram_sctp_rcvinfo struct */
            if (BIO_dgram_is_sctp(SSL_get_rbio(s))) {
                DTLS1_RECORD_DATA *rdata = (DTLS1_RECORD_DATA *)item->data;
                BIO_ctrl(SSL_get_rbio(s), BIO_CTRL_DGRAM_SCTP_SET_RCVINFO,
                         sizeof(rdata->recordinfo), &rdata->recordinfo);
            }
#endif

            dtls1_copy_record(s, item);

            OPENSSL_free(item->data);
            pitem_free(item);
        }
    }

    /* Check for timeout */
    if (dtls1_handle_timeout(s) > 0) {
        goto start;
    } else if (ossl_statem_in_error(s)) {
        /* dtls1_handle_timeout() has failed with a fatal error */
        return -1;
    }

    /* get new packet if necessary */
    if ((SSL3_RECORD_get_length(rr) == 0)
        || (s->rlayer.rstate == SSL_ST_READ_BODY)) {
        RECORD_LAYER_set_numrpipes(&s->rlayer, 0);
        iret = dtls1_get_record(s);
        if (iret <= 0) {
            iret = dtls1_read_failed(s, iret);
            /*
             * Anything other than a timeout is an error. SSLfatal() already
             * called if appropriate.
             */
            if (iret <= 0)
                return iret;
            else
                goto start;
        }
        RECORD_LAYER_set_numrpipes(&s->rlayer, 1);
    }

    /*
     * Reset the count of consecutive warning alerts if we've got a non-empty
     * record that isn't an alert.
     */
    if (SSL3_RECORD_get_type(rr) != SSL3_RT_ALERT
            && SSL3_RECORD_get_length(rr) != 0)
        s->rlayer.alert_count = 0;

    /* we now have a packet which can be read and processed */

    if (s->s3->change_cipher_spec /* set when we receive ChangeCipherSpec,
                                   * reset by ssl3_get_finished */
        && (SSL3_RECORD_get_type(rr) != SSL3_RT_HANDSHAKE)) {
        /*
         * We now have application data between CCS and Finished. Most likely
         * the packets were reordered on their way, so buffer the application
         * data for later processing rather than dropping the connection.
         */
        if (dtls1_buffer_record(s, &(s->rlayer.d->buffered_app_data),
                                SSL3_RECORD_get_seq_num(rr)) < 0) {
            /* SSLfatal() already called */
            return -1;
        }
        SSL3_RECORD_set_length(rr, 0);
        SSL3_RECORD_set_read(rr);
        goto start;
    }

    /*
     * If the other end has shut down, throw anything we read away (even in
     * 'peek' mode)
     */
    if (s->shutdown & SSL_RECEIVED_SHUTDOWN) {
        SSL3_RECORD_set_length(rr, 0);
        SSL3_RECORD_set_read(rr);
        s->rwstate = SSL_NOTHING;
        return 0;
    }

    if (type == SSL3_RECORD_get_type(rr)
        || (SSL3_RECORD_get_type(rr) == SSL3_RT_CHANGE_CIPHER_SPEC
            && type == SSL3_RT_HANDSHAKE && recvd_type != NULL)) {
        /*
         * SSL3_RT_APPLICATION_DATA or
         * SSL3_RT_HANDSHAKE or
         * SSL3_RT_CHANGE_CIPHER_SPEC
         */
        /*
         * make sure that we are not getting application data when we are
         * doing a handshake for the first time
         */
        if (SSL_in_init(s) && (type == SSL3_RT_APPLICATION_DATA) &&
            (s->enc_read_ctx == NULL)) {
            SSLfatal(s, SSL_AD_UNEXPECTED_MESSAGE, SSL_F_DTLS1_READ_BYTES,
                     SSL_R_APP_DATA_IN_HANDSHAKE);
            return -1;
        }

        if (recvd_type != NULL)
            *recvd_type = SSL3_RECORD_get_type(rr);

        if (len == 0) {
            /*
             * Mark a zero length record as read. This ensures multiple calls to
             * SSL_read() with a zero length buffer will eventually cause
             * SSL_pending() to report data as being available.
             */
            if (SSL3_RECORD_get_length(rr) == 0)
                SSL3_RECORD_set_read(rr);
            return 0;
        }

        if (len > SSL3_RECORD_get_length(rr))
            n = SSL3_RECORD_get_length(rr);
        else
            n = len;

        memcpy(buf, &(SSL3_RECORD_get_data(rr)[SSL3_RECORD_get_off(rr)]), n);
        if (peek) {
            if (SSL3_RECORD_get_length(rr) == 0)
                SSL3_RECORD_set_read(rr);
        } else {
            SSL3_RECORD_sub_length(rr, n);
            SSL3_RECORD_add_off(rr, n);
            if (SSL3_RECORD_get_length(rr) == 0) {
                s->rlayer.rstate = SSL_ST_READ_HEADER;
                SSL3_RECORD_set_off(rr, 0);
                SSL3_RECORD_set_read(rr);
            }
        }
#ifndef OPENSSL_NO_SCTP
        /*
         * We might had to delay a close_notify alert because of reordered
         * app data. If there was an alert and there is no message to read
         * anymore, finally set shutdown.
         */
        if (BIO_dgram_is_sctp(SSL_get_rbio(s)) &&
            s->d1->shutdown_received
            && !BIO_dgram_sctp_msg_waiting(SSL_get_rbio(s))) {
            s->shutdown |= SSL_RECEIVED_SHUTDOWN;
            return 0;
        }
#endif
        *readbytes = n;
        return 1;
    }

    /*
     * If we get here, then type != rr->type; if we have a handshake message,
     * then it was unexpected (Hello Request or Client Hello).
     */

    if (SSL3_RECORD_get_type(rr) == SSL3_RT_ALERT) {
        unsigned int alert_level, alert_descr;
        unsigned char *alert_bytes = SSL3_RECORD_get_data(rr)
                                     + SSL3_RECORD_get_off(rr);
        PACKET alert;

        if (!PACKET_buf_init(&alert, alert_bytes, SSL3_RECORD_get_length(rr))
                || !PACKET_get_1(&alert, &alert_level)
                || !PACKET_get_1(&alert, &alert_descr)
                || PACKET_remaining(&alert) != 0) {
            SSLfatal(s, SSL_AD_UNEXPECTED_MESSAGE, SSL_F_DTLS1_READ_BYTES,
                     SSL_R_INVALID_ALERT);
            return -1;
        }

        if (s->msg_callback)
            s->msg_callback(0, s->version, SSL3_RT_ALERT, alert_bytes, 2, s,
                            s->msg_callback_arg);

        if (s->info_callback != NULL)
            cb = s->info_callback;
        else if (s->ctx->info_callback != NULL)
            cb = s->ctx->info_callback;

        if (cb != NULL) {
            j = (alert_level << 8) | alert_descr;
            cb(s, SSL_CB_READ_ALERT, j);
        }

        if (alert_level == SSL3_AL_WARNING) {
            s->s3->warn_alert = alert_descr;
            SSL3_RECORD_set_read(rr);

            s->rlayer.alert_count++;
            if (s->rlayer.alert_count == MAX_WARN_ALERT_COUNT) {
                SSLfatal(s, SSL_AD_UNEXPECTED_MESSAGE, SSL_F_DTLS1_READ_BYTES,
                         SSL_R_TOO_MANY_WARN_ALERTS);
                return -1;
            }

            if (alert_descr == SSL_AD_CLOSE_NOTIFY) {
#ifndef OPENSSL_NO_SCTP
                /*
                 * With SCTP and streams the socket may deliver app data
                 * after a close_notify alert. We have to check this first so
                 * that nothing gets discarded.
                 */
                if (BIO_dgram_is_sctp(SSL_get_rbio(s)) &&
                    BIO_dgram_sctp_msg_waiting(SSL_get_rbio(s))) {
                    s->d1->shutdown_received = 1;
                    s->rwstate = SSL_READING;
                    BIO_clear_retry_flags(SSL_get_rbio(s));
                    BIO_set_retry_read(SSL_get_rbio(s));
                    return -1;
                }
#endif
                s->shutdown |= SSL_RECEIVED_SHUTDOWN;
                return 0;
            }
        } else if (alert_level == SSL3_AL_FATAL) {
            char tmp[16];

            s->rwstate = SSL_NOTHING;
            s->s3->fatal_alert = alert_descr;
            SSLfatal(s, SSL_AD_NO_ALERT, SSL_F_DTLS1_READ_BYTES,
                     SSL_AD_REASON_OFFSET + alert_descr);
            BIO_snprintf(tmp, sizeof tmp, "%d", alert_descr);
            ERR_add_error_data(2, "SSL alert number ", tmp);
            s->shutdown |= SSL_RECEIVED_SHUTDOWN;
            SSL3_RECORD_set_read(rr);
            SSL_CTX_remove_session(s->session_ctx, s->session);
            return 0;
        } else {
            SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER, SSL_F_DTLS1_READ_BYTES,
                     SSL_R_UNKNOWN_ALERT_TYPE);
            return -1;
        }

        goto start;
    }

    if (s->shutdown & SSL_SENT_SHUTDOWN) { /* but we have not received a
                                            * shutdown */
        s->rwstate = SSL_NOTHING;
        SSL3_RECORD_set_length(rr, 0);
        SSL3_RECORD_set_read(rr);
        return 0;
    }

    if (SSL3_RECORD_get_type(rr) == SSL3_RT_CHANGE_CIPHER_SPEC) {
        /*
         * We can't process a CCS now, because previous handshake messages
         * are still missing, so just drop it.
         */
        SSL3_RECORD_set_length(rr, 0);
        SSL3_RECORD_set_read(rr);
        goto start;
    }

    /*
     * Unexpected handshake message (Client Hello, or protocol violation)
     */
    if ((SSL3_RECORD_get_type(rr) == SSL3_RT_HANDSHAKE) &&
            !ossl_statem_get_in_handshake(s)) {
        struct hm_header_st msg_hdr;

        /*
         * This may just be a stale retransmit. Also sanity check that we have
         * at least enough record bytes for a message header
         */
        if (SSL3_RECORD_get_epoch(rr) != s->rlayer.d->r_epoch
                || SSL3_RECORD_get_length(rr) < DTLS1_HM_HEADER_LENGTH) {
            SSL3_RECORD_set_length(rr, 0);
            SSL3_RECORD_set_read(rr);
            goto start;
        }

        dtls1_get_message_header(rr->data, &msg_hdr);

        /*
         * If we are server, we may have a repeated FINISHED of the client
         * here, then retransmit our CCS and FINISHED.
         */
        if (msg_hdr.type == SSL3_MT_FINISHED) {
            if (dtls1_check_timeout_num(s) < 0) {
                /* SSLfatal) already called */
                return -1;
            }

            if (dtls1_retransmit_buffered_messages(s) <= 0) {
                /* Fail if we encountered a fatal error */
                if (ossl_statem_in_error(s))
                    return -1;
            }
            SSL3_RECORD_set_length(rr, 0);
            SSL3_RECORD_set_read(rr);
            if (!(s->mode & SSL_MODE_AUTO_RETRY)) {
                if (SSL3_BUFFER_get_left(&s->rlayer.rbuf) == 0) {
                    /* no read-ahead left? */
                    BIO *bio;

                    s->rwstate = SSL_READING;
                    bio = SSL_get_rbio(s);
                    BIO_clear_retry_flags(bio);
                    BIO_set_retry_read(bio);
                    return -1;
                }
            }
            goto start;
        }

        /*
         * To get here we must be trying to read app data but found handshake
         * data. But if we're trying to read app data, and we're not in init
         * (which is tested for at the top of this function) then init must be
         * finished
         */
        if (!ossl_assert(SSL_is_init_finished(s))) {
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DTLS1_READ_BYTES,
                     ERR_R_INTERNAL_ERROR);
            return -1;
        }

        /* We found handshake data, so we're going back into init */
        ossl_statem_set_in_init(s, 1);

        i = s->handshake_func(s);
        /* SSLfatal() called if appropriate */
        if (i < 0)
            return i;
        if (i == 0)
            return -1;

        if (!(s->mode & SSL_MODE_AUTO_RETRY)) {
            if (SSL3_BUFFER_get_left(&s->rlayer.rbuf) == 0) {
                /* no read-ahead left? */
                BIO *bio;
                /*
                 * In the case where we try to read application data, but we
                 * trigger an SSL handshake, we return -1 with the retry
                 * option set.  Otherwise renegotiation may cause nasty
                 * problems in the blocking world
                 */
                s->rwstate = SSL_READING;
                bio = SSL_get_rbio(s);
                BIO_clear_retry_flags(bio);
                BIO_set_retry_read(bio);
                return -1;
            }
        }
        goto start;
    }

    switch (SSL3_RECORD_get_type(rr)) {
    default:
        SSLfatal(s, SSL_AD_UNEXPECTED_MESSAGE, SSL_F_DTLS1_READ_BYTES,
                 SSL_R_UNEXPECTED_RECORD);
        return -1;
    case SSL3_RT_CHANGE_CIPHER_SPEC:
    case SSL3_RT_ALERT:
    case SSL3_RT_HANDSHAKE:
        /*
         * we already handled all of these, with the possible exception of
         * SSL3_RT_HANDSHAKE when ossl_statem_get_in_handshake(s) is true, but
         * that should not happen when type != rr->type
         */
        SSLfatal(s, SSL_AD_UNEXPECTED_MESSAGE, SSL_F_DTLS1_READ_BYTES,
                 ERR_R_INTERNAL_ERROR);
        return -1;
    case SSL3_RT_APPLICATION_DATA:
        /*
         * At this point, we were expecting handshake data, but have
         * application data.  If the library was running inside ssl3_read()
         * (i.e. in_read_app_data is set) and it makes sense to read
         * application data at this point (session renegotiation not yet
         * started), we will indulge it.
         */
        if (s->s3->in_read_app_data &&
            (s->s3->total_renegotiations != 0) &&
            ossl_statem_app_data_allowed(s)) {
            s->s3->in_read_app_data = 2;
            return -1;
        } else {
            SSLfatal(s, SSL_AD_UNEXPECTED_MESSAGE, SSL_F_DTLS1_READ_BYTES,
                     SSL_R_UNEXPECTED_RECORD);
            return -1;
        }
    }
    /* not reached */
}

/*
 * Call this to write data in records of type 'type' It will return <= 0 if
 * not all data has been sent or non-blocking IO.
 */
int dtls1_write_bytes(SSL *s, int type, const void *buf, size_t len,
                      size_t *written)
{
    int i;

    if (!ossl_assert(len <= SSL3_RT_MAX_PLAIN_LENGTH)) {
        SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DTLS1_WRITE_BYTES,
                 ERR_R_INTERNAL_ERROR);
        return -1;
    }
    s->rwstate = SSL_NOTHING;
    i = do_dtls1_write(s, type, buf, len, 0, written);
    return i;
}

int do_dtls1_write(SSL *s, int type, const unsigned char *buf,
                   size_t len, int create_empty_fragment, size_t *written)
{
    unsigned char *p, *pseq;
    int i, mac_size, clear = 0;
    size_t prefix_len = 0;
    int eivlen;
    SSL3_RECORD wr;
    SSL3_BUFFER *wb;
    SSL_SESSION *sess;

    wb = &s->rlayer.wbuf[0];

    /*
     * first check if there is a SSL3_BUFFER still being written out.  This
     * will happen with non blocking IO
     */
    if (!ossl_assert(SSL3_BUFFER_get_left(wb) == 0)) {
        SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DO_DTLS1_WRITE,
                 ERR_R_INTERNAL_ERROR);
        return 0;
    }

    /* If we have an alert to send, lets send it */
    if (s->s3->alert_dispatch) {
        i = s->method->ssl_dispatch_alert(s);
        if (i <= 0)
            return i;
        /* if it went, fall through and send more stuff */
    }

    if (len == 0 && !create_empty_fragment)
        return 0;

    if (len > ssl_get_max_send_fragment(s)) {
        SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DO_DTLS1_WRITE,
                 SSL_R_EXCEEDS_MAX_FRAGMENT_SIZE);
        return 0;
    }

    sess = s->session;

    if ((sess == NULL) ||
        (s->enc_write_ctx == NULL) || (EVP_MD_CTX_md(s->write_hash) == NULL))
        clear = 1;

    if (clear)
        mac_size = 0;
    else {
        mac_size = EVP_MD_CTX_size(s->write_hash);
        if (mac_size < 0) {
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DO_DTLS1_WRITE,
                     SSL_R_EXCEEDS_MAX_FRAGMENT_SIZE);
            return -1;
        }
    }

    p = SSL3_BUFFER_get_buf(wb) + prefix_len;

    /* write the header */

    *(p++) = type & 0xff;
    SSL3_RECORD_set_type(&wr, type);
    /*
     * Special case: for hello verify request, client version 1.0 and we
     * haven't decided which version to use yet send back using version 1.0
     * header: otherwise some clients will ignore it.
     */
    if (s->method->version == DTLS_ANY_VERSION &&
        s->max_proto_version != DTLS1_BAD_VER) {
        *(p++) = DTLS1_VERSION >> 8;
        *(p++) = DTLS1_VERSION & 0xff;
    } else {
        *(p++) = s->version >> 8;
        *(p++) = s->version & 0xff;
    }

    /* field where we are to write out packet epoch, seq num and len */
    pseq = p;
    p += 10;

    /* Explicit IV length, block ciphers appropriate version flag */
    if (s->enc_write_ctx) {
        int mode = EVP_CIPHER_CTX_mode(s->enc_write_ctx);
        if (mode == EVP_CIPH_CBC_MODE) {
            eivlen = EVP_CIPHER_CTX_iv_length(s->enc_write_ctx);
            if (eivlen <= 1)
                eivlen = 0;
        }
        /* Need explicit part of IV for GCM mode */
        else if (mode == EVP_CIPH_GCM_MODE)
            eivlen = EVP_GCM_TLS_EXPLICIT_IV_LEN;
        else if (mode == EVP_CIPH_CCM_MODE)
            eivlen = EVP_CCM_TLS_EXPLICIT_IV_LEN;
        else
            eivlen = 0;
    } else
        eivlen = 0;

    /* lets setup the record stuff. */
    SSL3_RECORD_set_data(&wr, p + eivlen); /* make room for IV in case of CBC */
    SSL3_RECORD_set_length(&wr, len);
    SSL3_RECORD_set_input(&wr, (unsigned char *)buf);

    /*
     * we now 'read' from wr.input, wr.length bytes into wr.data
     */

    /* first we compress */
    if (s->compress != NULL) {
        if (!ssl3_do_compress(s, &wr)) {
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DO_DTLS1_WRITE,
                     SSL_R_COMPRESSION_FAILURE);
            return -1;
        }
    } else {
        memcpy(SSL3_RECORD_get_data(&wr), SSL3_RECORD_get_input(&wr),
               SSL3_RECORD_get_length(&wr));
        SSL3_RECORD_reset_input(&wr);
    }

    /*
     * we should still have the output to wr.data and the input from
     * wr.input.  Length should be wr.length. wr.data still points in the
     * wb->buf
     */

    if (!SSL_WRITE_ETM(s) && mac_size != 0) {
        if (!s->method->ssl3_enc->mac(s, &wr,
                                      &(p[SSL3_RECORD_get_length(&wr) + eivlen]),
                                      1)) {
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DO_DTLS1_WRITE,
                     ERR_R_INTERNAL_ERROR);
            return -1;
        }
        SSL3_RECORD_add_length(&wr, mac_size);
    }

    /* this is true regardless of mac size */
    SSL3_RECORD_set_data(&wr, p);
    SSL3_RECORD_reset_input(&wr);

    if (eivlen)
        SSL3_RECORD_add_length(&wr, eivlen);

    if (s->method->ssl3_enc->enc(s, &wr, 1, 1) < 1) {
        if (!ossl_statem_in_error(s)) {
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DO_DTLS1_WRITE,
                     ERR_R_INTERNAL_ERROR);
        }
        return -1;
    }

    if (SSL_WRITE_ETM(s) && mac_size != 0) {
        if (!s->method->ssl3_enc->mac(s, &wr,
                                      &(p[SSL3_RECORD_get_length(&wr)]), 1)) {
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DO_DTLS1_WRITE,
                     ERR_R_INTERNAL_ERROR);
            return -1;
        }
        SSL3_RECORD_add_length(&wr, mac_size);
    }

    /* record length after mac and block padding */

    /* there's only one epoch between handshake and app data */

    s2n(s->rlayer.d->w_epoch, pseq);

    memcpy(pseq, &(s->rlayer.write_sequence[2]), 6);
    pseq += 6;
    s2n(SSL3_RECORD_get_length(&wr), pseq);

    if (s->msg_callback)
        s->msg_callback(1, 0, SSL3_RT_HEADER, pseq - DTLS1_RT_HEADER_LENGTH,
                        DTLS1_RT_HEADER_LENGTH, s, s->msg_callback_arg);

    /*
     * we should now have wr.data pointing to the encrypted data, which is
     * wr->length long
     */
    SSL3_RECORD_set_type(&wr, type); /* not needed but helps for debugging */
    SSL3_RECORD_add_length(&wr, DTLS1_RT_HEADER_LENGTH);

    ssl3_record_sequence_update(&(s->rlayer.write_sequence[0]));

    if (create_empty_fragment) {
        /*
         * we are in a recursive call; just return the length, don't write
         * out anything here
         */
        *written = wr.length;
        return 1;
    }

    /* now let's set up wb */
    SSL3_BUFFER_set_left(wb, prefix_len + SSL3_RECORD_get_length(&wr));
    SSL3_BUFFER_set_offset(wb, 0);

    /*
     * memorize arguments so that ssl3_write_pending can detect bad write
     * retries later
     */
    s->rlayer.wpend_tot = len;
    s->rlayer.wpend_buf = buf;
    s->rlayer.wpend_type = type;
    s->rlayer.wpend_ret = len;

    /* we now just need to write the buffer. Calls SSLfatal() as required. */
    return ssl3_write_pending(s, type, buf, len, written);
}

DTLS1_BITMAP *dtls1_get_bitmap(SSL *s, SSL3_RECORD *rr,
                               unsigned int *is_next_epoch)
{

    *is_next_epoch = 0;

    /* In current epoch, accept HM, CCS, DATA, & ALERT */
    if (rr->epoch == s->rlayer.d->r_epoch)
        return &s->rlayer.d->bitmap;

    /*
     * Only HM and ALERT messages can be from the next epoch and only if we
     * have already processed all of the unprocessed records from the last
     * epoch
     */
    else if (rr->epoch == (unsigned long)(s->rlayer.d->r_epoch + 1) &&
             s->rlayer.d->unprocessed_rcds.epoch != s->rlayer.d->r_epoch &&
             (rr->type == SSL3_RT_HANDSHAKE || rr->type == SSL3_RT_ALERT)) {
        *is_next_epoch = 1;
        return &s->rlayer.d->next_bitmap;
    }

    return NULL;
}

void dtls1_reset_seq_numbers(SSL *s, int rw)
{
    unsigned char *seq;
    unsigned int seq_bytes = sizeof(s->rlayer.read_sequence);

    if (rw & SSL3_CC_READ) {
        seq = s->rlayer.read_sequence;
        s->rlayer.d->r_epoch++;
        memcpy(&s->rlayer.d->bitmap, &s->rlayer.d->next_bitmap,
               sizeof(s->rlayer.d->bitmap));
        memset(&s->rlayer.d->next_bitmap, 0, sizeof(s->rlayer.d->next_bitmap));

        /*
         * We must not use any buffered messages received from the previous
         * epoch
         */
        dtls1_clear_received_buffer(s);
    } else {
        seq = s->rlayer.write_sequence;
        memcpy(s->rlayer.d->last_write_sequence, seq,
               sizeof(s->rlayer.write_sequence));
        s->rlayer.d->w_epoch++;
    }

    memset(seq, 0, seq_bytes);
}
