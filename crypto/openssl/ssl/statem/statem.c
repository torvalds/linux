/*
 * Copyright 2015-2019 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/cryptlib.h"
#include <openssl/rand.h>
#include "../ssl_locl.h"
#include "statem_locl.h"
#include <assert.h>

/*
 * This file implements the SSL/TLS/DTLS state machines.
 *
 * There are two primary state machines:
 *
 * 1) Message flow state machine
 * 2) Handshake state machine
 *
 * The Message flow state machine controls the reading and sending of messages
 * including handling of non-blocking IO events, flushing of the underlying
 * write BIO, handling unexpected messages, etc. It is itself broken into two
 * separate sub-state machines which control reading and writing respectively.
 *
 * The Handshake state machine keeps track of the current SSL/TLS handshake
 * state. Transitions of the handshake state are the result of events that
 * occur within the Message flow state machine.
 *
 * Overall it looks like this:
 *
 * ---------------------------------------------            -------------------
 * |                                           |            |                 |
 * | Message flow state machine                |            |                 |
 * |                                           |            |                 |
 * | -------------------- -------------------- | Transition | Handshake state |
 * | | MSG_FLOW_READING | | MSG_FLOW_WRITING | | Event      | machine         |
 * | | sub-state        | | sub-state        | |----------->|                 |
 * | | machine for      | | machine for      | |            |                 |
 * | | reading messages | | writing messages | |            |                 |
 * | -------------------- -------------------- |            |                 |
 * |                                           |            |                 |
 * ---------------------------------------------            -------------------
 *
 */

/* Sub state machine return values */
typedef enum {
    /* Something bad happened or NBIO */
    SUB_STATE_ERROR,
    /* Sub state finished go to the next sub state */
    SUB_STATE_FINISHED,
    /* Sub state finished and handshake was completed */
    SUB_STATE_END_HANDSHAKE
} SUB_STATE_RETURN;

static int state_machine(SSL *s, int server);
static void init_read_state_machine(SSL *s);
static SUB_STATE_RETURN read_state_machine(SSL *s);
static void init_write_state_machine(SSL *s);
static SUB_STATE_RETURN write_state_machine(SSL *s);

OSSL_HANDSHAKE_STATE SSL_get_state(const SSL *ssl)
{
    return ssl->statem.hand_state;
}

int SSL_in_init(const SSL *s)
{
    return s->statem.in_init;
}

int SSL_is_init_finished(const SSL *s)
{
    return !(s->statem.in_init) && (s->statem.hand_state == TLS_ST_OK);
}

int SSL_in_before(const SSL *s)
{
    /*
     * Historically being "in before" meant before anything had happened. In the
     * current code though we remain in the "before" state for a while after we
     * have started the handshake process (e.g. as a server waiting for the
     * first message to arrive). There "in before" is taken to mean "in before"
     * and not started any handshake process yet.
     */
    return (s->statem.hand_state == TLS_ST_BEFORE)
        && (s->statem.state == MSG_FLOW_UNINITED);
}

/*
 * Clear the state machine state and reset back to MSG_FLOW_UNINITED
 */
void ossl_statem_clear(SSL *s)
{
    s->statem.state = MSG_FLOW_UNINITED;
    s->statem.hand_state = TLS_ST_BEFORE;
    s->statem.in_init = 1;
    s->statem.no_cert_verify = 0;
}

/*
 * Set the state machine up ready for a renegotiation handshake
 */
void ossl_statem_set_renegotiate(SSL *s)
{
    s->statem.in_init = 1;
    s->statem.request_state = TLS_ST_SW_HELLO_REQ;
}

/*
 * Put the state machine into an error state and send an alert if appropriate.
 * This is a permanent error for the current connection.
 */
void ossl_statem_fatal(SSL *s, int al, int func, int reason, const char *file,
                       int line)
{
    ERR_put_error(ERR_LIB_SSL, func, reason, file, line);
    /* We shouldn't call SSLfatal() twice. Once is enough */
    if (s->statem.in_init && s->statem.state == MSG_FLOW_ERROR)
      return;
    s->statem.in_init = 1;
    s->statem.state = MSG_FLOW_ERROR;
    if (al != SSL_AD_NO_ALERT
            && s->statem.enc_write_state != ENC_WRITE_STATE_INVALID)
        ssl3_send_alert(s, SSL3_AL_FATAL, al);
}

/*
 * This macro should only be called if we are already expecting to be in
 * a fatal error state. We verify that we are, and set it if not (this would
 * indicate a bug).
 */
#define check_fatal(s, f) \
    do { \
        if (!ossl_assert((s)->statem.in_init \
                         && (s)->statem.state == MSG_FLOW_ERROR)) \
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, (f), \
                     SSL_R_MISSING_FATAL); \
    } while (0)

/*
 * Discover whether the current connection is in the error state.
 *
 * Valid return values are:
 *   1: Yes
 *   0: No
 */
int ossl_statem_in_error(const SSL *s)
{
    if (s->statem.state == MSG_FLOW_ERROR)
        return 1;

    return 0;
}

void ossl_statem_set_in_init(SSL *s, int init)
{
    s->statem.in_init = init;
}

int ossl_statem_get_in_handshake(SSL *s)
{
    return s->statem.in_handshake;
}

void ossl_statem_set_in_handshake(SSL *s, int inhand)
{
    if (inhand)
        s->statem.in_handshake++;
    else
        s->statem.in_handshake--;
}

/* Are we in a sensible state to skip over unreadable early data? */
int ossl_statem_skip_early_data(SSL *s)
{
    if (s->ext.early_data != SSL_EARLY_DATA_REJECTED)
        return 0;

    if (!s->server
            || s->statem.hand_state != TLS_ST_EARLY_DATA
            || s->hello_retry_request == SSL_HRR_COMPLETE)
        return 0;

    return 1;
}

/*
 * Called when we are in SSL_read*(), SSL_write*(), or SSL_accept()
 * /SSL_connect()/SSL_do_handshake(). Used to test whether we are in an early
 * data state and whether we should attempt to move the handshake on if so.
 * |sending| is 1 if we are attempting to send data (SSL_write*()), 0 if we are
 * attempting to read data (SSL_read*()), or -1 if we are in SSL_do_handshake()
 * or similar.
 */
void ossl_statem_check_finish_init(SSL *s, int sending)
{
    if (sending == -1) {
        if (s->statem.hand_state == TLS_ST_PENDING_EARLY_DATA_END
                || s->statem.hand_state == TLS_ST_EARLY_DATA) {
            ossl_statem_set_in_init(s, 1);
            if (s->early_data_state == SSL_EARLY_DATA_WRITE_RETRY) {
                /*
                 * SSL_connect() or SSL_do_handshake() has been called directly.
                 * We don't allow any more writing of early data.
                 */
                s->early_data_state = SSL_EARLY_DATA_FINISHED_WRITING;
            }
        }
    } else if (!s->server) {
        if ((sending && (s->statem.hand_state == TLS_ST_PENDING_EARLY_DATA_END
                      || s->statem.hand_state == TLS_ST_EARLY_DATA)
                  && s->early_data_state != SSL_EARLY_DATA_WRITING)
                || (!sending && s->statem.hand_state == TLS_ST_EARLY_DATA)) {
            ossl_statem_set_in_init(s, 1);
            /*
             * SSL_write() has been called directly. We don't allow any more
             * writing of early data.
             */
            if (sending && s->early_data_state == SSL_EARLY_DATA_WRITE_RETRY)
                s->early_data_state = SSL_EARLY_DATA_FINISHED_WRITING;
        }
    } else {
        if (s->early_data_state == SSL_EARLY_DATA_FINISHED_READING
                && s->statem.hand_state == TLS_ST_EARLY_DATA)
            ossl_statem_set_in_init(s, 1);
    }
}

void ossl_statem_set_hello_verify_done(SSL *s)
{
    s->statem.state = MSG_FLOW_UNINITED;
    s->statem.in_init = 1;
    /*
     * This will get reset (briefly) back to TLS_ST_BEFORE when we enter
     * state_machine() because |state| is MSG_FLOW_UNINITED, but until then any
     * calls to SSL_in_before() will return false. Also calls to
     * SSL_state_string() and SSL_state_string_long() will return something
     * sensible.
     */
    s->statem.hand_state = TLS_ST_SR_CLNT_HELLO;
}

int ossl_statem_connect(SSL *s)
{
    return state_machine(s, 0);
}

int ossl_statem_accept(SSL *s)
{
    return state_machine(s, 1);
}

typedef void (*info_cb) (const SSL *, int, int);

static info_cb get_callback(SSL *s)
{
    if (s->info_callback != NULL)
        return s->info_callback;
    else if (s->ctx->info_callback != NULL)
        return s->ctx->info_callback;

    return NULL;
}

/*
 * The main message flow state machine. We start in the MSG_FLOW_UNINITED or
 * MSG_FLOW_FINISHED state and finish in MSG_FLOW_FINISHED. Valid states and
 * transitions are as follows:
 *
 * MSG_FLOW_UNINITED     MSG_FLOW_FINISHED
 *        |                       |
 *        +-----------------------+
 *        v
 * MSG_FLOW_WRITING <---> MSG_FLOW_READING
 *        |
 *        V
 * MSG_FLOW_FINISHED
 *        |
 *        V
 *    [SUCCESS]
 *
 * We may exit at any point due to an error or NBIO event. If an NBIO event
 * occurs then we restart at the point we left off when we are recalled.
 * MSG_FLOW_WRITING and MSG_FLOW_READING have sub-state machines associated with them.
 *
 * In addition to the above there is also the MSG_FLOW_ERROR state. We can move
 * into that state at any point in the event that an irrecoverable error occurs.
 *
 * Valid return values are:
 *   1: Success
 * <=0: NBIO or error
 */
static int state_machine(SSL *s, int server)
{
    BUF_MEM *buf = NULL;
    void (*cb) (const SSL *ssl, int type, int val) = NULL;
    OSSL_STATEM *st = &s->statem;
    int ret = -1;
    int ssret;

    if (st->state == MSG_FLOW_ERROR) {
        /* Shouldn't have been called if we're already in the error state */
        return -1;
    }

    ERR_clear_error();
    clear_sys_error();

    cb = get_callback(s);

    st->in_handshake++;
    if (!SSL_in_init(s) || SSL_in_before(s)) {
        /*
         * If we are stateless then we already called SSL_clear() - don't do
         * it again and clear the STATELESS flag itself.
         */
        if ((s->s3->flags & TLS1_FLAGS_STATELESS) == 0 && !SSL_clear(s))
            return -1;
    }
#ifndef OPENSSL_NO_SCTP
    if (SSL_IS_DTLS(s) && BIO_dgram_is_sctp(SSL_get_wbio(s))) {
        /*
         * Notify SCTP BIO socket to enter handshake mode and prevent stream
         * identifier other than 0.
         */
        BIO_ctrl(SSL_get_wbio(s), BIO_CTRL_DGRAM_SCTP_SET_IN_HANDSHAKE,
                 st->in_handshake, NULL);
    }
#endif

    /* Initialise state machine */
    if (st->state == MSG_FLOW_UNINITED
            || st->state == MSG_FLOW_FINISHED) {
        if (st->state == MSG_FLOW_UNINITED) {
            st->hand_state = TLS_ST_BEFORE;
            st->request_state = TLS_ST_BEFORE;
        }

        s->server = server;
        if (cb != NULL) {
            if (SSL_IS_FIRST_HANDSHAKE(s) || !SSL_IS_TLS13(s))
                cb(s, SSL_CB_HANDSHAKE_START, 1);
        }

        /*
         * Fatal errors in this block don't send an alert because we have
         * failed to even initialise properly. Sending an alert is probably
         * doomed to failure.
         */

        if (SSL_IS_DTLS(s)) {
            if ((s->version & 0xff00) != (DTLS1_VERSION & 0xff00) &&
                (server || (s->version & 0xff00) != (DTLS1_BAD_VER & 0xff00))) {
                SSLfatal(s, SSL_AD_NO_ALERT, SSL_F_STATE_MACHINE,
                         ERR_R_INTERNAL_ERROR);
                goto end;
            }
        } else {
            if ((s->version >> 8) != SSL3_VERSION_MAJOR) {
                SSLfatal(s, SSL_AD_NO_ALERT, SSL_F_STATE_MACHINE,
                         ERR_R_INTERNAL_ERROR);
                goto end;
            }
        }

        if (!ssl_security(s, SSL_SECOP_VERSION, 0, s->version, NULL)) {
            SSLfatal(s, SSL_AD_NO_ALERT, SSL_F_STATE_MACHINE,
                     ERR_R_INTERNAL_ERROR);
            goto end;
        }

        if (s->init_buf == NULL) {
            if ((buf = BUF_MEM_new()) == NULL) {
                SSLfatal(s, SSL_AD_NO_ALERT, SSL_F_STATE_MACHINE,
                         ERR_R_INTERNAL_ERROR);
                goto end;
            }
            if (!BUF_MEM_grow(buf, SSL3_RT_MAX_PLAIN_LENGTH)) {
                SSLfatal(s, SSL_AD_NO_ALERT, SSL_F_STATE_MACHINE,
                         ERR_R_INTERNAL_ERROR);
                goto end;
            }
            s->init_buf = buf;
            buf = NULL;
        }

        if (!ssl3_setup_buffers(s)) {
            SSLfatal(s, SSL_AD_NO_ALERT, SSL_F_STATE_MACHINE,
                     ERR_R_INTERNAL_ERROR);
            goto end;
        }
        s->init_num = 0;

        /*
         * Should have been reset by tls_process_finished, too.
         */
        s->s3->change_cipher_spec = 0;

        /*
         * Ok, we now need to push on a buffering BIO ...but not with
         * SCTP
         */
#ifndef OPENSSL_NO_SCTP
        if (!SSL_IS_DTLS(s) || !BIO_dgram_is_sctp(SSL_get_wbio(s)))
#endif
            if (!ssl_init_wbio_buffer(s)) {
                SSLfatal(s, SSL_AD_NO_ALERT, SSL_F_STATE_MACHINE,
                         ERR_R_INTERNAL_ERROR);
                goto end;
            }

        if ((SSL_in_before(s))
                || s->renegotiate) {
            if (!tls_setup_handshake(s)) {
                /* SSLfatal() already called */
                goto end;
            }

            if (SSL_IS_FIRST_HANDSHAKE(s))
                st->read_state_first_init = 1;
        }

        st->state = MSG_FLOW_WRITING;
        init_write_state_machine(s);
    }

    while (st->state != MSG_FLOW_FINISHED) {
        if (st->state == MSG_FLOW_READING) {
            ssret = read_state_machine(s);
            if (ssret == SUB_STATE_FINISHED) {
                st->state = MSG_FLOW_WRITING;
                init_write_state_machine(s);
            } else {
                /* NBIO or error */
                goto end;
            }
        } else if (st->state == MSG_FLOW_WRITING) {
            ssret = write_state_machine(s);
            if (ssret == SUB_STATE_FINISHED) {
                st->state = MSG_FLOW_READING;
                init_read_state_machine(s);
            } else if (ssret == SUB_STATE_END_HANDSHAKE) {
                st->state = MSG_FLOW_FINISHED;
            } else {
                /* NBIO or error */
                goto end;
            }
        } else {
            /* Error */
            check_fatal(s, SSL_F_STATE_MACHINE);
            SSLerr(SSL_F_STATE_MACHINE, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
            goto end;
        }
    }

    ret = 1;

 end:
    st->in_handshake--;

#ifndef OPENSSL_NO_SCTP
    if (SSL_IS_DTLS(s) && BIO_dgram_is_sctp(SSL_get_wbio(s))) {
        /*
         * Notify SCTP BIO socket to leave handshake mode and allow stream
         * identifier other than 0.
         */
        BIO_ctrl(SSL_get_wbio(s), BIO_CTRL_DGRAM_SCTP_SET_IN_HANDSHAKE,
                 st->in_handshake, NULL);
    }
#endif

    BUF_MEM_free(buf);
    if (cb != NULL) {
        if (server)
            cb(s, SSL_CB_ACCEPT_EXIT, ret);
        else
            cb(s, SSL_CB_CONNECT_EXIT, ret);
    }
    return ret;
}

/*
 * Initialise the MSG_FLOW_READING sub-state machine
 */
static void init_read_state_machine(SSL *s)
{
    OSSL_STATEM *st = &s->statem;

    st->read_state = READ_STATE_HEADER;
}

static int grow_init_buf(SSL *s, size_t size) {

    size_t msg_offset = (char *)s->init_msg - s->init_buf->data;

    if (!BUF_MEM_grow_clean(s->init_buf, (int)size))
        return 0;

    if (size < msg_offset)
        return 0;

    s->init_msg = s->init_buf->data + msg_offset;

    return 1;
}

/*
 * This function implements the sub-state machine when the message flow is in
 * MSG_FLOW_READING. The valid sub-states and transitions are:
 *
 * READ_STATE_HEADER <--+<-------------+
 *        |             |              |
 *        v             |              |
 * READ_STATE_BODY -----+-->READ_STATE_POST_PROCESS
 *        |                            |
 *        +----------------------------+
 *        v
 * [SUB_STATE_FINISHED]
 *
 * READ_STATE_HEADER has the responsibility for reading in the message header
 * and transitioning the state of the handshake state machine.
 *
 * READ_STATE_BODY reads in the rest of the message and then subsequently
 * processes it.
 *
 * READ_STATE_POST_PROCESS is an optional step that may occur if some post
 * processing activity performed on the message may block.
 *
 * Any of the above states could result in an NBIO event occurring in which case
 * control returns to the calling application. When this function is recalled we
 * will resume in the same state where we left off.
 */
static SUB_STATE_RETURN read_state_machine(SSL *s)
{
    OSSL_STATEM *st = &s->statem;
    int ret, mt;
    size_t len = 0;
    int (*transition) (SSL *s, int mt);
    PACKET pkt;
    MSG_PROCESS_RETURN(*process_message) (SSL *s, PACKET *pkt);
    WORK_STATE(*post_process_message) (SSL *s, WORK_STATE wst);
    size_t (*max_message_size) (SSL *s);
    void (*cb) (const SSL *ssl, int type, int val) = NULL;

    cb = get_callback(s);

    if (s->server) {
        transition = ossl_statem_server_read_transition;
        process_message = ossl_statem_server_process_message;
        max_message_size = ossl_statem_server_max_message_size;
        post_process_message = ossl_statem_server_post_process_message;
    } else {
        transition = ossl_statem_client_read_transition;
        process_message = ossl_statem_client_process_message;
        max_message_size = ossl_statem_client_max_message_size;
        post_process_message = ossl_statem_client_post_process_message;
    }

    if (st->read_state_first_init) {
        s->first_packet = 1;
        st->read_state_first_init = 0;
    }

    while (1) {
        switch (st->read_state) {
        case READ_STATE_HEADER:
            /* Get the state the peer wants to move to */
            if (SSL_IS_DTLS(s)) {
                /*
                 * In DTLS we get the whole message in one go - header and body
                 */
                ret = dtls_get_message(s, &mt, &len);
            } else {
                ret = tls_get_message_header(s, &mt);
            }

            if (ret == 0) {
                /* Could be non-blocking IO */
                return SUB_STATE_ERROR;
            }

            if (cb != NULL) {
                /* Notify callback of an impending state change */
                if (s->server)
                    cb(s, SSL_CB_ACCEPT_LOOP, 1);
                else
                    cb(s, SSL_CB_CONNECT_LOOP, 1);
            }
            /*
             * Validate that we are allowed to move to the new state and move
             * to that state if so
             */
            if (!transition(s, mt))
                return SUB_STATE_ERROR;

            if (s->s3->tmp.message_size > max_message_size(s)) {
                SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER, SSL_F_READ_STATE_MACHINE,
                         SSL_R_EXCESSIVE_MESSAGE_SIZE);
                return SUB_STATE_ERROR;
            }

            /* dtls_get_message already did this */
            if (!SSL_IS_DTLS(s)
                    && s->s3->tmp.message_size > 0
                    && !grow_init_buf(s, s->s3->tmp.message_size
                                         + SSL3_HM_HEADER_LENGTH)) {
                SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_READ_STATE_MACHINE,
                         ERR_R_BUF_LIB);
                return SUB_STATE_ERROR;
            }

            st->read_state = READ_STATE_BODY;
            /* Fall through */

        case READ_STATE_BODY:
            if (!SSL_IS_DTLS(s)) {
                /* We already got this above for DTLS */
                ret = tls_get_message_body(s, &len);
                if (ret == 0) {
                    /* Could be non-blocking IO */
                    return SUB_STATE_ERROR;
                }
            }

            s->first_packet = 0;
            if (!PACKET_buf_init(&pkt, s->init_msg, len)) {
                SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_READ_STATE_MACHINE,
                         ERR_R_INTERNAL_ERROR);
                return SUB_STATE_ERROR;
            }
            ret = process_message(s, &pkt);

            /* Discard the packet data */
            s->init_num = 0;

            switch (ret) {
            case MSG_PROCESS_ERROR:
                check_fatal(s, SSL_F_READ_STATE_MACHINE);
                return SUB_STATE_ERROR;

            case MSG_PROCESS_FINISHED_READING:
                if (SSL_IS_DTLS(s)) {
                    dtls1_stop_timer(s);
                }
                return SUB_STATE_FINISHED;

            case MSG_PROCESS_CONTINUE_PROCESSING:
                st->read_state = READ_STATE_POST_PROCESS;
                st->read_state_work = WORK_MORE_A;
                break;

            default:
                st->read_state = READ_STATE_HEADER;
                break;
            }
            break;

        case READ_STATE_POST_PROCESS:
            st->read_state_work = post_process_message(s, st->read_state_work);
            switch (st->read_state_work) {
            case WORK_ERROR:
                check_fatal(s, SSL_F_READ_STATE_MACHINE);
                /* Fall through */
            case WORK_MORE_A:
            case WORK_MORE_B:
            case WORK_MORE_C:
                return SUB_STATE_ERROR;

            case WORK_FINISHED_CONTINUE:
                st->read_state = READ_STATE_HEADER;
                break;

            case WORK_FINISHED_STOP:
                if (SSL_IS_DTLS(s)) {
                    dtls1_stop_timer(s);
                }
                return SUB_STATE_FINISHED;
            }
            break;

        default:
            /* Shouldn't happen */
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_READ_STATE_MACHINE,
                     ERR_R_INTERNAL_ERROR);
            return SUB_STATE_ERROR;
        }
    }
}

/*
 * Send a previously constructed message to the peer.
 */
static int statem_do_write(SSL *s)
{
    OSSL_STATEM *st = &s->statem;

    if (st->hand_state == TLS_ST_CW_CHANGE
        || st->hand_state == TLS_ST_SW_CHANGE) {
        if (SSL_IS_DTLS(s))
            return dtls1_do_write(s, SSL3_RT_CHANGE_CIPHER_SPEC);
        else
            return ssl3_do_write(s, SSL3_RT_CHANGE_CIPHER_SPEC);
    } else {
        return ssl_do_write(s);
    }
}

/*
 * Initialise the MSG_FLOW_WRITING sub-state machine
 */
static void init_write_state_machine(SSL *s)
{
    OSSL_STATEM *st = &s->statem;

    st->write_state = WRITE_STATE_TRANSITION;
}

/*
 * This function implements the sub-state machine when the message flow is in
 * MSG_FLOW_WRITING. The valid sub-states and transitions are:
 *
 * +-> WRITE_STATE_TRANSITION ------> [SUB_STATE_FINISHED]
 * |             |
 * |             v
 * |      WRITE_STATE_PRE_WORK -----> [SUB_STATE_END_HANDSHAKE]
 * |             |
 * |             v
 * |       WRITE_STATE_SEND
 * |             |
 * |             v
 * |     WRITE_STATE_POST_WORK
 * |             |
 * +-------------+
 *
 * WRITE_STATE_TRANSITION transitions the state of the handshake state machine

 * WRITE_STATE_PRE_WORK performs any work necessary to prepare the later
 * sending of the message. This could result in an NBIO event occurring in
 * which case control returns to the calling application. When this function
 * is recalled we will resume in the same state where we left off.
 *
 * WRITE_STATE_SEND sends the message and performs any work to be done after
 * sending.
 *
 * WRITE_STATE_POST_WORK performs any work necessary after the sending of the
 * message has been completed. As for WRITE_STATE_PRE_WORK this could also
 * result in an NBIO event.
 */
static SUB_STATE_RETURN write_state_machine(SSL *s)
{
    OSSL_STATEM *st = &s->statem;
    int ret;
    WRITE_TRAN(*transition) (SSL *s);
    WORK_STATE(*pre_work) (SSL *s, WORK_STATE wst);
    WORK_STATE(*post_work) (SSL *s, WORK_STATE wst);
    int (*get_construct_message_f) (SSL *s, WPACKET *pkt,
                                    int (**confunc) (SSL *s, WPACKET *pkt),
                                    int *mt);
    void (*cb) (const SSL *ssl, int type, int val) = NULL;
    int (*confunc) (SSL *s, WPACKET *pkt);
    int mt;
    WPACKET pkt;

    cb = get_callback(s);

    if (s->server) {
        transition = ossl_statem_server_write_transition;
        pre_work = ossl_statem_server_pre_work;
        post_work = ossl_statem_server_post_work;
        get_construct_message_f = ossl_statem_server_construct_message;
    } else {
        transition = ossl_statem_client_write_transition;
        pre_work = ossl_statem_client_pre_work;
        post_work = ossl_statem_client_post_work;
        get_construct_message_f = ossl_statem_client_construct_message;
    }

    while (1) {
        switch (st->write_state) {
        case WRITE_STATE_TRANSITION:
            if (cb != NULL) {
                /* Notify callback of an impending state change */
                if (s->server)
                    cb(s, SSL_CB_ACCEPT_LOOP, 1);
                else
                    cb(s, SSL_CB_CONNECT_LOOP, 1);
            }
            switch (transition(s)) {
            case WRITE_TRAN_CONTINUE:
                st->write_state = WRITE_STATE_PRE_WORK;
                st->write_state_work = WORK_MORE_A;
                break;

            case WRITE_TRAN_FINISHED:
                return SUB_STATE_FINISHED;
                break;

            case WRITE_TRAN_ERROR:
                check_fatal(s, SSL_F_WRITE_STATE_MACHINE);
                return SUB_STATE_ERROR;
            }
            break;

        case WRITE_STATE_PRE_WORK:
            switch (st->write_state_work = pre_work(s, st->write_state_work)) {
            case WORK_ERROR:
                check_fatal(s, SSL_F_WRITE_STATE_MACHINE);
                /* Fall through */
            case WORK_MORE_A:
            case WORK_MORE_B:
            case WORK_MORE_C:
                return SUB_STATE_ERROR;

            case WORK_FINISHED_CONTINUE:
                st->write_state = WRITE_STATE_SEND;
                break;

            case WORK_FINISHED_STOP:
                return SUB_STATE_END_HANDSHAKE;
            }
            if (!get_construct_message_f(s, &pkt, &confunc, &mt)) {
                /* SSLfatal() already called */
                return SUB_STATE_ERROR;
            }
            if (mt == SSL3_MT_DUMMY) {
                /* Skip construction and sending. This isn't a "real" state */
                st->write_state = WRITE_STATE_POST_WORK;
                st->write_state_work = WORK_MORE_A;
                break;
            }
            if (!WPACKET_init(&pkt, s->init_buf)
                    || !ssl_set_handshake_header(s, &pkt, mt)) {
                WPACKET_cleanup(&pkt);
                SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_WRITE_STATE_MACHINE,
                         ERR_R_INTERNAL_ERROR);
                return SUB_STATE_ERROR;
            }
            if (confunc != NULL && !confunc(s, &pkt)) {
                WPACKET_cleanup(&pkt);
                check_fatal(s, SSL_F_WRITE_STATE_MACHINE);
                return SUB_STATE_ERROR;
            }
            if (!ssl_close_construct_packet(s, &pkt, mt)
                    || !WPACKET_finish(&pkt)) {
                WPACKET_cleanup(&pkt);
                SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_WRITE_STATE_MACHINE,
                         ERR_R_INTERNAL_ERROR);
                return SUB_STATE_ERROR;
            }

            /* Fall through */

        case WRITE_STATE_SEND:
            if (SSL_IS_DTLS(s) && st->use_timer) {
                dtls1_start_timer(s);
            }
            ret = statem_do_write(s);
            if (ret <= 0) {
                return SUB_STATE_ERROR;
            }
            st->write_state = WRITE_STATE_POST_WORK;
            st->write_state_work = WORK_MORE_A;
            /* Fall through */

        case WRITE_STATE_POST_WORK:
            switch (st->write_state_work = post_work(s, st->write_state_work)) {
            case WORK_ERROR:
                check_fatal(s, SSL_F_WRITE_STATE_MACHINE);
                /* Fall through */
            case WORK_MORE_A:
            case WORK_MORE_B:
            case WORK_MORE_C:
                return SUB_STATE_ERROR;

            case WORK_FINISHED_CONTINUE:
                st->write_state = WRITE_STATE_TRANSITION;
                break;

            case WORK_FINISHED_STOP:
                return SUB_STATE_END_HANDSHAKE;
            }
            break;

        default:
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_WRITE_STATE_MACHINE,
                     ERR_R_INTERNAL_ERROR);
            return SUB_STATE_ERROR;
        }
    }
}

/*
 * Flush the write BIO
 */
int statem_flush(SSL *s)
{
    s->rwstate = SSL_WRITING;
    if (BIO_flush(s->wbio) <= 0) {
        return 0;
    }
    s->rwstate = SSL_NOTHING;

    return 1;
}

/*
 * Called by the record layer to determine whether application data is
 * allowed to be received in the current handshake state or not.
 *
 * Return values are:
 *   1: Yes (application data allowed)
 *   0: No (application data not allowed)
 */
int ossl_statem_app_data_allowed(SSL *s)
{
    OSSL_STATEM *st = &s->statem;

    if (st->state == MSG_FLOW_UNINITED)
        return 0;

    if (!s->s3->in_read_app_data || (s->s3->total_renegotiations == 0))
        return 0;

    if (s->server) {
        /*
         * If we're a server and we haven't got as far as writing our
         * ServerHello yet then we allow app data
         */
        if (st->hand_state == TLS_ST_BEFORE
            || st->hand_state == TLS_ST_SR_CLNT_HELLO)
            return 1;
    } else {
        /*
         * If we're a client and we haven't read the ServerHello yet then we
         * allow app data
         */
        if (st->hand_state == TLS_ST_CW_CLNT_HELLO)
            return 1;
    }

    return 0;
}

/*
 * This function returns 1 if TLS exporter is ready to export keying
 * material, or 0 if otherwise.
 */
int ossl_statem_export_allowed(SSL *s)
{
    return s->s3->previous_server_finished_len != 0
           && s->statem.hand_state != TLS_ST_SW_FINISHED;
}

/*
 * Return 1 if early TLS exporter is ready to export keying material,
 * or 0 if otherwise.
 */
int ossl_statem_export_early_allowed(SSL *s)
{
    /*
     * The early exporter secret is only present on the server if we
     * have accepted early_data. It is present on the client as long
     * as we have sent early_data.
     */
    return s->ext.early_data == SSL_EARLY_DATA_ACCEPTED
           || (!s->server && s->ext.early_data != SSL_EARLY_DATA_NOT_SENT);
}
