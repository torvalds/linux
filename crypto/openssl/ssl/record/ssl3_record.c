/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "../ssl_locl.h"
#include "internal/constant_time_locl.h"
#include <openssl/rand.h>
#include "record_locl.h"
#include "internal/cryptlib.h"

static const unsigned char ssl3_pad_1[48] = {
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
};

static const unsigned char ssl3_pad_2[48] = {
    0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
    0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
    0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
    0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
    0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
    0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c
};

/*
 * Clear the contents of an SSL3_RECORD but retain any memory allocated
 */
void SSL3_RECORD_clear(SSL3_RECORD *r, size_t num_recs)
{
    unsigned char *comp;
    size_t i;

    for (i = 0; i < num_recs; i++) {
        comp = r[i].comp;

        memset(&r[i], 0, sizeof(*r));
        r[i].comp = comp;
    }
}

void SSL3_RECORD_release(SSL3_RECORD *r, size_t num_recs)
{
    size_t i;

    for (i = 0; i < num_recs; i++) {
        OPENSSL_free(r[i].comp);
        r[i].comp = NULL;
    }
}

void SSL3_RECORD_set_seq_num(SSL3_RECORD *r, const unsigned char *seq_num)
{
    memcpy(r->seq_num, seq_num, SEQ_NUM_SIZE);
}

/*
 * Peeks ahead into "read_ahead" data to see if we have a whole record waiting
 * for us in the buffer.
 */
static int ssl3_record_app_data_waiting(SSL *s)
{
    SSL3_BUFFER *rbuf;
    size_t left, len;
    unsigned char *p;

    rbuf = RECORD_LAYER_get_rbuf(&s->rlayer);

    p = SSL3_BUFFER_get_buf(rbuf);
    if (p == NULL)
        return 0;

    left = SSL3_BUFFER_get_left(rbuf);

    if (left < SSL3_RT_HEADER_LENGTH)
        return 0;

    p += SSL3_BUFFER_get_offset(rbuf);

    /*
     * We only check the type and record length, we will sanity check version
     * etc later
     */
    if (*p != SSL3_RT_APPLICATION_DATA)
        return 0;

    p += 3;
    n2s(p, len);

    if (left < SSL3_RT_HEADER_LENGTH + len)
        return 0;

    return 1;
}

int early_data_count_ok(SSL *s, size_t length, size_t overhead, int send)
{
    uint32_t max_early_data;
    SSL_SESSION *sess = s->session;

    /*
     * If we are a client then we always use the max_early_data from the
     * session/psksession. Otherwise we go with the lowest out of the max early
     * data set in the session and the configured max_early_data.
     */
    if (!s->server && sess->ext.max_early_data == 0) {
        if (!ossl_assert(s->psksession != NULL
                         && s->psksession->ext.max_early_data > 0)) {
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_EARLY_DATA_COUNT_OK,
                     ERR_R_INTERNAL_ERROR);
            return 0;
        }
        sess = s->psksession;
    }

    if (!s->server)
        max_early_data = sess->ext.max_early_data;
    else if (s->ext.early_data != SSL_EARLY_DATA_ACCEPTED)
        max_early_data = s->recv_max_early_data;
    else
        max_early_data = s->recv_max_early_data < sess->ext.max_early_data
                         ? s->recv_max_early_data : sess->ext.max_early_data;

    if (max_early_data == 0) {
        SSLfatal(s, send ? SSL_AD_INTERNAL_ERROR : SSL_AD_UNEXPECTED_MESSAGE,
                 SSL_F_EARLY_DATA_COUNT_OK, SSL_R_TOO_MUCH_EARLY_DATA);
        return 0;
    }

    /* If we are dealing with ciphertext we need to allow for the overhead */
    max_early_data += overhead;

    if (s->early_data_count + length > max_early_data) {
        SSLfatal(s, send ? SSL_AD_INTERNAL_ERROR : SSL_AD_UNEXPECTED_MESSAGE,
                 SSL_F_EARLY_DATA_COUNT_OK, SSL_R_TOO_MUCH_EARLY_DATA);
        return 0;
    }
    s->early_data_count += length;

    return 1;
}

/*
 * MAX_EMPTY_RECORDS defines the number of consecutive, empty records that
 * will be processed per call to ssl3_get_record. Without this limit an
 * attacker could send empty records at a faster rate than we can process and
 * cause ssl3_get_record to loop forever.
 */
#define MAX_EMPTY_RECORDS 32

#define SSL2_RT_HEADER_LENGTH   2
/*-
 * Call this to get new input records.
 * It will return <= 0 if more data is needed, normally due to an error
 * or non-blocking IO.
 * When it finishes, |numrpipes| records have been decoded. For each record 'i':
 * rr[i].type    - is the type of record
 * rr[i].data,   - data
 * rr[i].length, - number of bytes
 * Multiple records will only be returned if the record types are all
 * SSL3_RT_APPLICATION_DATA. The number of records returned will always be <=
 * |max_pipelines|
 */
/* used only by ssl3_read_bytes */
int ssl3_get_record(SSL *s)
{
    int enc_err, rret;
    int i;
    size_t more, n;
    SSL3_RECORD *rr, *thisrr;
    SSL3_BUFFER *rbuf;
    SSL_SESSION *sess;
    unsigned char *p;
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int version;
    size_t mac_size;
    int imac_size;
    size_t num_recs = 0, max_recs, j;
    PACKET pkt, sslv2pkt;
    size_t first_rec_len;

    rr = RECORD_LAYER_get_rrec(&s->rlayer);
    rbuf = RECORD_LAYER_get_rbuf(&s->rlayer);
    max_recs = s->max_pipelines;
    if (max_recs == 0)
        max_recs = 1;
    sess = s->session;

    do {
        thisrr = &rr[num_recs];

        /* check if we have the header */
        if ((RECORD_LAYER_get_rstate(&s->rlayer) != SSL_ST_READ_BODY) ||
            (RECORD_LAYER_get_packet_length(&s->rlayer)
             < SSL3_RT_HEADER_LENGTH)) {
            size_t sslv2len;
            unsigned int type;

            rret = ssl3_read_n(s, SSL3_RT_HEADER_LENGTH,
                               SSL3_BUFFER_get_len(rbuf), 0,
                               num_recs == 0 ? 1 : 0, &n);
            if (rret <= 0)
                return rret;     /* error or non-blocking */
            RECORD_LAYER_set_rstate(&s->rlayer, SSL_ST_READ_BODY);

            p = RECORD_LAYER_get_packet(&s->rlayer);
            if (!PACKET_buf_init(&pkt, RECORD_LAYER_get_packet(&s->rlayer),
                                 RECORD_LAYER_get_packet_length(&s->rlayer))) {
                SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_SSL3_GET_RECORD,
                         ERR_R_INTERNAL_ERROR);
                return -1;
            }
            sslv2pkt = pkt;
            if (!PACKET_get_net_2_len(&sslv2pkt, &sslv2len)
                    || !PACKET_get_1(&sslv2pkt, &type)) {
                SSLfatal(s, SSL_AD_DECODE_ERROR, SSL_F_SSL3_GET_RECORD,
                         ERR_R_INTERNAL_ERROR);
                return -1;
            }
            /*
             * The first record received by the server may be a V2ClientHello.
             */
            if (s->server && RECORD_LAYER_is_first_record(&s->rlayer)
                    && (sslv2len & 0x8000) != 0
                    && (type == SSL2_MT_CLIENT_HELLO)) {
                /*
                 *  SSLv2 style record
                 *
                 * |num_recs| here will actually always be 0 because
                 * |num_recs > 0| only ever occurs when we are processing
                 * multiple app data records - which we know isn't the case here
                 * because it is an SSLv2ClientHello. We keep it using
                 * |num_recs| for the sake of consistency
                 */
                thisrr->type = SSL3_RT_HANDSHAKE;
                thisrr->rec_version = SSL2_VERSION;

                thisrr->length = sslv2len & 0x7fff;

                if (thisrr->length > SSL3_BUFFER_get_len(rbuf)
                    - SSL2_RT_HEADER_LENGTH) {
                    SSLfatal(s, SSL_AD_RECORD_OVERFLOW, SSL_F_SSL3_GET_RECORD,
                             SSL_R_PACKET_LENGTH_TOO_LONG);
                    return -1;
                }

                if (thisrr->length < MIN_SSL2_RECORD_LEN) {
                    SSLfatal(s, SSL_AD_DECODE_ERROR, SSL_F_SSL3_GET_RECORD,
                             SSL_R_LENGTH_TOO_SHORT);
                    return -1;
                }
            } else {
                /* SSLv3+ style record */
                if (s->msg_callback)
                    s->msg_callback(0, 0, SSL3_RT_HEADER, p, 5, s,
                                    s->msg_callback_arg);

                /* Pull apart the header into the SSL3_RECORD */
                if (!PACKET_get_1(&pkt, &type)
                        || !PACKET_get_net_2(&pkt, &version)
                        || !PACKET_get_net_2_len(&pkt, &thisrr->length)) {
                    SSLfatal(s, SSL_AD_DECODE_ERROR, SSL_F_SSL3_GET_RECORD,
                             ERR_R_INTERNAL_ERROR);
                    return -1;
                }
                thisrr->type = type;
                thisrr->rec_version = version;

                /*
                 * Lets check version. In TLSv1.3 we only check this field
                 * when encryption is occurring (see later check). For the
                 * ServerHello after an HRR we haven't actually selected TLSv1.3
                 * yet, but we still treat it as TLSv1.3, so we must check for
                 * that explicitly
                 */
                if (!s->first_packet && !SSL_IS_TLS13(s)
                        && s->hello_retry_request != SSL_HRR_PENDING
                        && version != (unsigned int)s->version) {
                    if ((s->version & 0xFF00) == (version & 0xFF00)
                        && !s->enc_write_ctx && !s->write_hash) {
                        if (thisrr->type == SSL3_RT_ALERT) {
                            /*
                             * The record is using an incorrect version number,
                             * but what we've got appears to be an alert. We
                             * haven't read the body yet to check whether its a
                             * fatal or not - but chances are it is. We probably
                             * shouldn't send a fatal alert back. We'll just
                             * end.
                             */
                            SSLfatal(s, SSL_AD_NO_ALERT, SSL_F_SSL3_GET_RECORD,
                                     SSL_R_WRONG_VERSION_NUMBER);
                            return -1;
                        }
                        /*
                         * Send back error using their minor version number :-)
                         */
                        s->version = (unsigned short)version;
                    }
                    SSLfatal(s, SSL_AD_PROTOCOL_VERSION, SSL_F_SSL3_GET_RECORD,
                             SSL_R_WRONG_VERSION_NUMBER);
                    return -1;
                }

                if ((version >> 8) != SSL3_VERSION_MAJOR) {
                    if (RECORD_LAYER_is_first_record(&s->rlayer)) {
                        /* Go back to start of packet, look at the five bytes
                         * that we have. */
                        p = RECORD_LAYER_get_packet(&s->rlayer);
                        if (strncmp((char *)p, "GET ", 4) == 0 ||
                            strncmp((char *)p, "POST ", 5) == 0 ||
                            strncmp((char *)p, "HEAD ", 5) == 0 ||
                            strncmp((char *)p, "PUT ", 4) == 0) {
                            SSLfatal(s, SSL_AD_NO_ALERT, SSL_F_SSL3_GET_RECORD,
                                     SSL_R_HTTP_REQUEST);
                            return -1;
                        } else if (strncmp((char *)p, "CONNE", 5) == 0) {
                            SSLfatal(s, SSL_AD_NO_ALERT, SSL_F_SSL3_GET_RECORD,
                                     SSL_R_HTTPS_PROXY_REQUEST);
                            return -1;
                        }

                        /* Doesn't look like TLS - don't send an alert */
                        SSLfatal(s, SSL_AD_NO_ALERT, SSL_F_SSL3_GET_RECORD,
                                 SSL_R_WRONG_VERSION_NUMBER);
                        return -1;
                    } else {
                        SSLfatal(s, SSL_AD_PROTOCOL_VERSION,
                                 SSL_F_SSL3_GET_RECORD,
                                 SSL_R_WRONG_VERSION_NUMBER);
                        return -1;
                    }
                }

                if (SSL_IS_TLS13(s) && s->enc_read_ctx != NULL) {
                    if (thisrr->type != SSL3_RT_APPLICATION_DATA
                            && (thisrr->type != SSL3_RT_CHANGE_CIPHER_SPEC
                                || !SSL_IS_FIRST_HANDSHAKE(s))
                            && (thisrr->type != SSL3_RT_ALERT
                                || s->statem.enc_read_state
                                   != ENC_READ_STATE_ALLOW_PLAIN_ALERTS)) {
                        SSLfatal(s, SSL_AD_UNEXPECTED_MESSAGE,
                                 SSL_F_SSL3_GET_RECORD, SSL_R_BAD_RECORD_TYPE);
                        return -1;
                    }
                    if (thisrr->rec_version != TLS1_2_VERSION) {
                        SSLfatal(s, SSL_AD_DECODE_ERROR, SSL_F_SSL3_GET_RECORD,
                                 SSL_R_WRONG_VERSION_NUMBER);
                        return -1;
                    }
                }

                if (thisrr->length >
                    SSL3_BUFFER_get_len(rbuf) - SSL3_RT_HEADER_LENGTH) {
                    SSLfatal(s, SSL_AD_RECORD_OVERFLOW, SSL_F_SSL3_GET_RECORD,
                             SSL_R_PACKET_LENGTH_TOO_LONG);
                    return -1;
                }
            }

            /* now s->rlayer.rstate == SSL_ST_READ_BODY */
        }

        if (SSL_IS_TLS13(s)) {
            if (thisrr->length > SSL3_RT_MAX_TLS13_ENCRYPTED_LENGTH) {
                SSLfatal(s, SSL_AD_RECORD_OVERFLOW, SSL_F_SSL3_GET_RECORD,
                         SSL_R_ENCRYPTED_LENGTH_TOO_LONG);
                return -1;
            }
        } else {
            size_t len = SSL3_RT_MAX_ENCRYPTED_LENGTH;

#ifndef OPENSSL_NO_COMP
            /*
             * If OPENSSL_NO_COMP is defined then SSL3_RT_MAX_ENCRYPTED_LENGTH
             * does not include the compression overhead anyway.
             */
            if (s->expand == NULL)
                len -= SSL3_RT_MAX_COMPRESSED_OVERHEAD;
#endif

            if (thisrr->length > len) {
                SSLfatal(s, SSL_AD_RECORD_OVERFLOW, SSL_F_SSL3_GET_RECORD,
                         SSL_R_ENCRYPTED_LENGTH_TOO_LONG);
                return -1;
            }
        }

        /*
         * s->rlayer.rstate == SSL_ST_READ_BODY, get and decode the data.
         * Calculate how much more data we need to read for the rest of the
         * record
         */
        if (thisrr->rec_version == SSL2_VERSION) {
            more = thisrr->length + SSL2_RT_HEADER_LENGTH
                - SSL3_RT_HEADER_LENGTH;
        } else {
            more = thisrr->length;
        }
        if (more > 0) {
            /* now s->packet_length == SSL3_RT_HEADER_LENGTH */

            rret = ssl3_read_n(s, more, more, 1, 0, &n);
            if (rret <= 0)
                return rret;     /* error or non-blocking io */
        }

        /* set state for later operations */
        RECORD_LAYER_set_rstate(&s->rlayer, SSL_ST_READ_HEADER);

        /*
         * At this point, s->packet_length == SSL3_RT_HEADER_LENGTH
         * + thisrr->length, or s->packet_length == SSL2_RT_HEADER_LENGTH
         * + thisrr->length and we have that many bytes in s->packet
         */
        if (thisrr->rec_version == SSL2_VERSION) {
            thisrr->input =
                &(RECORD_LAYER_get_packet(&s->rlayer)[SSL2_RT_HEADER_LENGTH]);
        } else {
            thisrr->input =
                &(RECORD_LAYER_get_packet(&s->rlayer)[SSL3_RT_HEADER_LENGTH]);
        }

        /*
         * ok, we can now read from 's->packet' data into 'thisrr' thisrr->input
         * points at thisrr->length bytes, which need to be copied into
         * thisrr->data by either the decryption or by the decompression When
         * the data is 'copied' into the thisrr->data buffer, thisrr->input will
         * be pointed at the new buffer
         */

        /*
         * We now have - encrypted [ MAC [ compressed [ plain ] ] ]
         * thisrr->length bytes of encrypted compressed stuff.
         */

        /* decrypt in place in 'thisrr->input' */
        thisrr->data = thisrr->input;
        thisrr->orig_len = thisrr->length;

        /* Mark this record as not read by upper layers yet */
        thisrr->read = 0;

        num_recs++;

        /* we have pulled in a full packet so zero things */
        RECORD_LAYER_reset_packet_length(&s->rlayer);
        RECORD_LAYER_clear_first_record(&s->rlayer);
    } while (num_recs < max_recs
             && thisrr->type == SSL3_RT_APPLICATION_DATA
             && SSL_USE_EXPLICIT_IV(s)
             && s->enc_read_ctx != NULL
             && (EVP_CIPHER_flags(EVP_CIPHER_CTX_cipher(s->enc_read_ctx))
                 & EVP_CIPH_FLAG_PIPELINE)
             && ssl3_record_app_data_waiting(s));

    if (num_recs == 1
            && thisrr->type == SSL3_RT_CHANGE_CIPHER_SPEC
            && (SSL_IS_TLS13(s) || s->hello_retry_request != SSL_HRR_NONE)
            && SSL_IS_FIRST_HANDSHAKE(s)) {
        /*
         * CCS messages must be exactly 1 byte long, containing the value 0x01
         */
        if (thisrr->length != 1 || thisrr->data[0] != 0x01) {
            SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER, SSL_F_SSL3_GET_RECORD,
                     SSL_R_INVALID_CCS_MESSAGE);
            return -1;
        }
        /*
         * CCS messages are ignored in TLSv1.3. We treat it like an empty
         * handshake record
         */
        thisrr->type = SSL3_RT_HANDSHAKE;
        RECORD_LAYER_inc_empty_record_count(&s->rlayer);
        if (RECORD_LAYER_get_empty_record_count(&s->rlayer)
            > MAX_EMPTY_RECORDS) {
            SSLfatal(s, SSL_AD_UNEXPECTED_MESSAGE, SSL_F_SSL3_GET_RECORD,
                     SSL_R_UNEXPECTED_CCS_MESSAGE);
            return -1;
        }
        thisrr->read = 1;
        RECORD_LAYER_set_numrpipes(&s->rlayer, 1);

        return 1;
    }

    /*
     * If in encrypt-then-mac mode calculate mac from encrypted record. All
     * the details below are public so no timing details can leak.
     */
    if (SSL_READ_ETM(s) && s->read_hash) {
        unsigned char *mac;
        /* TODO(size_t): convert this to do size_t properly */
        imac_size = EVP_MD_CTX_size(s->read_hash);
        if (!ossl_assert(imac_size >= 0 && imac_size <= EVP_MAX_MD_SIZE)) {
                SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_SSL3_GET_RECORD,
                         ERR_LIB_EVP);
                return -1;
        }
        mac_size = (size_t)imac_size;
        for (j = 0; j < num_recs; j++) {
            thisrr = &rr[j];

            if (thisrr->length < mac_size) {
                SSLfatal(s, SSL_AD_DECODE_ERROR, SSL_F_SSL3_GET_RECORD,
                         SSL_R_LENGTH_TOO_SHORT);
                return -1;
            }
            thisrr->length -= mac_size;
            mac = thisrr->data + thisrr->length;
            i = s->method->ssl3_enc->mac(s, thisrr, md, 0 /* not send */ );
            if (i == 0 || CRYPTO_memcmp(md, mac, mac_size) != 0) {
                SSLfatal(s, SSL_AD_BAD_RECORD_MAC, SSL_F_SSL3_GET_RECORD,
                       SSL_R_DECRYPTION_FAILED_OR_BAD_RECORD_MAC);
                return -1;
            }
        }
    }

    first_rec_len = rr[0].length;

    enc_err = s->method->ssl3_enc->enc(s, rr, num_recs, 0);

    /*-
     * enc_err is:
     *    0: (in non-constant time) if the record is publicly invalid.
     *    1: if the padding is valid
     *    -1: if the padding is invalid
     */
    if (enc_err == 0) {
        if (ossl_statem_in_error(s)) {
            /* SSLfatal() already got called */
            return -1;
        }
        if (num_recs == 1 && ossl_statem_skip_early_data(s)) {
            /*
             * Valid early_data that we cannot decrypt might fail here as
             * publicly invalid. We treat it like an empty record.
             */

            thisrr = &rr[0];

            if (!early_data_count_ok(s, thisrr->length,
                                     EARLY_DATA_CIPHERTEXT_OVERHEAD, 0)) {
                /* SSLfatal() already called */
                return -1;
            }

            thisrr->length = 0;
            thisrr->read = 1;
            RECORD_LAYER_set_numrpipes(&s->rlayer, 1);
            RECORD_LAYER_reset_read_sequence(&s->rlayer);
            return 1;
        }
        SSLfatal(s, SSL_AD_DECRYPTION_FAILED, SSL_F_SSL3_GET_RECORD,
                 SSL_R_BLOCK_CIPHER_PAD_IS_WRONG);
        return -1;
    }
#ifdef SSL_DEBUG
    printf("dec %lu\n", (unsigned long)rr[0].length);
    {
        size_t z;
        for (z = 0; z < rr[0].length; z++)
            printf("%02X%c", rr[0].data[z], ((z + 1) % 16) ? ' ' : '\n');
    }
    printf("\n");
#endif

    /* r->length is now the compressed data plus mac */
    if ((sess != NULL) &&
        (s->enc_read_ctx != NULL) &&
        (!SSL_READ_ETM(s) && EVP_MD_CTX_md(s->read_hash) != NULL)) {
        /* s->read_hash != NULL => mac_size != -1 */
        unsigned char *mac = NULL;
        unsigned char mac_tmp[EVP_MAX_MD_SIZE];

        mac_size = EVP_MD_CTX_size(s->read_hash);
        if (!ossl_assert(mac_size <= EVP_MAX_MD_SIZE)) {
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_SSL3_GET_RECORD,
                     ERR_R_INTERNAL_ERROR);
            return -1;
        }

        for (j = 0; j < num_recs; j++) {
            thisrr = &rr[j];
            /*
             * orig_len is the length of the record before any padding was
             * removed. This is public information, as is the MAC in use,
             * therefore we can safely process the record in a different amount
             * of time if it's too short to possibly contain a MAC.
             */
            if (thisrr->orig_len < mac_size ||
                /* CBC records must have a padding length byte too. */
                (EVP_CIPHER_CTX_mode(s->enc_read_ctx) == EVP_CIPH_CBC_MODE &&
                 thisrr->orig_len < mac_size + 1)) {
                SSLfatal(s, SSL_AD_DECODE_ERROR, SSL_F_SSL3_GET_RECORD,
                         SSL_R_LENGTH_TOO_SHORT);
                return -1;
            }

            if (EVP_CIPHER_CTX_mode(s->enc_read_ctx) == EVP_CIPH_CBC_MODE) {
                /*
                 * We update the length so that the TLS header bytes can be
                 * constructed correctly but we need to extract the MAC in
                 * constant time from within the record, without leaking the
                 * contents of the padding bytes.
                 */
                mac = mac_tmp;
                if (!ssl3_cbc_copy_mac(mac_tmp, thisrr, mac_size)) {
                    SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_SSL3_GET_RECORD,
                             ERR_R_INTERNAL_ERROR);
                    return -1;
                }
                thisrr->length -= mac_size;
            } else {
                /*
                 * In this case there's no padding, so |rec->orig_len| equals
                 * |rec->length| and we checked that there's enough bytes for
                 * |mac_size| above.
                 */
                thisrr->length -= mac_size;
                mac = &thisrr->data[thisrr->length];
            }

            i = s->method->ssl3_enc->mac(s, thisrr, md, 0 /* not send */ );
            if (i == 0 || mac == NULL
                || CRYPTO_memcmp(md, mac, (size_t)mac_size) != 0)
                enc_err = -1;
            if (thisrr->length > SSL3_RT_MAX_COMPRESSED_LENGTH + mac_size)
                enc_err = -1;
        }
    }

    if (enc_err < 0) {
        if (ossl_statem_in_error(s)) {
            /* We already called SSLfatal() */
            return -1;
        }
        if (num_recs == 1 && ossl_statem_skip_early_data(s)) {
            /*
             * We assume this is unreadable early_data - we treat it like an
             * empty record
             */

            /*
             * The record length may have been modified by the mac check above
             * so we use the previously saved value
             */
            if (!early_data_count_ok(s, first_rec_len,
                                     EARLY_DATA_CIPHERTEXT_OVERHEAD, 0)) {
                /* SSLfatal() already called */
                return -1;
            }

            thisrr = &rr[0];
            thisrr->length = 0;
            thisrr->read = 1;
            RECORD_LAYER_set_numrpipes(&s->rlayer, 1);
            RECORD_LAYER_reset_read_sequence(&s->rlayer);
            return 1;
        }
        /*
         * A separate 'decryption_failed' alert was introduced with TLS 1.0,
         * SSL 3.0 only has 'bad_record_mac'.  But unless a decryption
         * failure is directly visible from the ciphertext anyway, we should
         * not reveal which kind of error occurred -- this might become
         * visible to an attacker (e.g. via a logfile)
         */
        SSLfatal(s, SSL_AD_BAD_RECORD_MAC, SSL_F_SSL3_GET_RECORD,
                 SSL_R_DECRYPTION_FAILED_OR_BAD_RECORD_MAC);
        return -1;
    }

    for (j = 0; j < num_recs; j++) {
        thisrr = &rr[j];

        /* thisrr->length is now just compressed */
        if (s->expand != NULL) {
            if (thisrr->length > SSL3_RT_MAX_COMPRESSED_LENGTH) {
                SSLfatal(s, SSL_AD_RECORD_OVERFLOW, SSL_F_SSL3_GET_RECORD,
                         SSL_R_COMPRESSED_LENGTH_TOO_LONG);
                return -1;
            }
            if (!ssl3_do_uncompress(s, thisrr)) {
                SSLfatal(s, SSL_AD_DECOMPRESSION_FAILURE, SSL_F_SSL3_GET_RECORD,
                         SSL_R_BAD_DECOMPRESSION);
                return -1;
            }
        }

        if (SSL_IS_TLS13(s)
                && s->enc_read_ctx != NULL
                && thisrr->type != SSL3_RT_ALERT) {
            size_t end;

            if (thisrr->length == 0
                    || thisrr->type != SSL3_RT_APPLICATION_DATA) {
                SSLfatal(s, SSL_AD_UNEXPECTED_MESSAGE, SSL_F_SSL3_GET_RECORD,
                         SSL_R_BAD_RECORD_TYPE);
                return -1;
            }

            /* Strip trailing padding */
            for (end = thisrr->length - 1; end > 0 && thisrr->data[end] == 0;
                 end--)
                continue;

            thisrr->length = end;
            thisrr->type = thisrr->data[end];
            if (thisrr->type != SSL3_RT_APPLICATION_DATA
                    && thisrr->type != SSL3_RT_ALERT
                    && thisrr->type != SSL3_RT_HANDSHAKE) {
                SSLfatal(s, SSL_AD_UNEXPECTED_MESSAGE, SSL_F_SSL3_GET_RECORD,
                         SSL_R_BAD_RECORD_TYPE);
                return -1;
            }
            if (s->msg_callback)
                s->msg_callback(0, s->version, SSL3_RT_INNER_CONTENT_TYPE,
                                &thisrr->data[end], 1, s, s->msg_callback_arg);
        }

        /*
         * TLSv1.3 alert and handshake records are required to be non-zero in
         * length.
         */
        if (SSL_IS_TLS13(s)
                && (thisrr->type == SSL3_RT_HANDSHAKE
                    || thisrr->type == SSL3_RT_ALERT)
                && thisrr->length == 0) {
            SSLfatal(s, SSL_AD_UNEXPECTED_MESSAGE, SSL_F_SSL3_GET_RECORD,
                     SSL_R_BAD_LENGTH);
            return -1;
        }

        if (thisrr->length > SSL3_RT_MAX_PLAIN_LENGTH) {
            SSLfatal(s, SSL_AD_RECORD_OVERFLOW, SSL_F_SSL3_GET_RECORD,
                     SSL_R_DATA_LENGTH_TOO_LONG);
            return -1;
        }

        /* If received packet overflows current Max Fragment Length setting */
        if (s->session != NULL && USE_MAX_FRAGMENT_LENGTH_EXT(s->session)
                && thisrr->length > GET_MAX_FRAGMENT_LENGTH(s->session)) {
            SSLfatal(s, SSL_AD_RECORD_OVERFLOW, SSL_F_SSL3_GET_RECORD,
                     SSL_R_DATA_LENGTH_TOO_LONG);
            return -1;
        }

        thisrr->off = 0;
        /*-
         * So at this point the following is true
         * thisrr->type   is the type of record
         * thisrr->length == number of bytes in record
         * thisrr->off    == offset to first valid byte
         * thisrr->data   == where to take bytes from, increment after use :-).
         */

        /* just read a 0 length packet */
        if (thisrr->length == 0) {
            RECORD_LAYER_inc_empty_record_count(&s->rlayer);
            if (RECORD_LAYER_get_empty_record_count(&s->rlayer)
                > MAX_EMPTY_RECORDS) {
                SSLfatal(s, SSL_AD_UNEXPECTED_MESSAGE, SSL_F_SSL3_GET_RECORD,
                         SSL_R_RECORD_TOO_SMALL);
                return -1;
            }
        } else {
            RECORD_LAYER_reset_empty_record_count(&s->rlayer);
        }
    }

    if (s->early_data_state == SSL_EARLY_DATA_READING) {
        thisrr = &rr[0];
        if (thisrr->type == SSL3_RT_APPLICATION_DATA
                && !early_data_count_ok(s, thisrr->length, 0, 0)) {
            /* SSLfatal already called */
            return -1;
        }
    }

    RECORD_LAYER_set_numrpipes(&s->rlayer, num_recs);
    return 1;
}

int ssl3_do_uncompress(SSL *ssl, SSL3_RECORD *rr)
{
#ifndef OPENSSL_NO_COMP
    int i;

    if (rr->comp == NULL) {
        rr->comp = (unsigned char *)
            OPENSSL_malloc(SSL3_RT_MAX_ENCRYPTED_LENGTH);
    }
    if (rr->comp == NULL)
        return 0;

    /* TODO(size_t): Convert this call */
    i = COMP_expand_block(ssl->expand, rr->comp,
                          SSL3_RT_MAX_PLAIN_LENGTH, rr->data, (int)rr->length);
    if (i < 0)
        return 0;
    else
        rr->length = i;
    rr->data = rr->comp;
#endif
    return 1;
}

int ssl3_do_compress(SSL *ssl, SSL3_RECORD *wr)
{
#ifndef OPENSSL_NO_COMP
    int i;

    /* TODO(size_t): Convert this call */
    i = COMP_compress_block(ssl->compress, wr->data,
                            (int)(wr->length + SSL3_RT_MAX_COMPRESSED_OVERHEAD),
                            wr->input, (int)wr->length);
    if (i < 0)
        return 0;
    else
        wr->length = i;

    wr->input = wr->data;
#endif
    return 1;
}

/*-
 * ssl3_enc encrypts/decrypts |n_recs| records in |inrecs|.  Will call
 * SSLfatal() for internal errors, but not otherwise.
 *
 * Returns:
 *   0: (in non-constant time) if the record is publically invalid (i.e. too
 *       short etc).
 *   1: if the record's padding is valid / the encryption was successful.
 *   -1: if the record's padding is invalid or, if sending, an internal error
 *       occurred.
 */
int ssl3_enc(SSL *s, SSL3_RECORD *inrecs, size_t n_recs, int sending)
{
    SSL3_RECORD *rec;
    EVP_CIPHER_CTX *ds;
    size_t l, i;
    size_t bs, mac_size = 0;
    int imac_size;
    const EVP_CIPHER *enc;

    rec = inrecs;
    /*
     * We shouldn't ever be called with more than one record in the SSLv3 case
     */
    if (n_recs != 1)
        return 0;
    if (sending) {
        ds = s->enc_write_ctx;
        if (s->enc_write_ctx == NULL)
            enc = NULL;
        else
            enc = EVP_CIPHER_CTX_cipher(s->enc_write_ctx);
    } else {
        ds = s->enc_read_ctx;
        if (s->enc_read_ctx == NULL)
            enc = NULL;
        else
            enc = EVP_CIPHER_CTX_cipher(s->enc_read_ctx);
    }

    if ((s->session == NULL) || (ds == NULL) || (enc == NULL)) {
        memmove(rec->data, rec->input, rec->length);
        rec->input = rec->data;
    } else {
        l = rec->length;
        /* TODO(size_t): Convert this call */
        bs = EVP_CIPHER_CTX_block_size(ds);

        /* COMPRESS */

        if ((bs != 1) && sending) {
            i = bs - (l % bs);

            /* we need to add 'i-1' padding bytes */
            l += i;
            /*
             * the last of these zero bytes will be overwritten with the
             * padding length.
             */
            memset(&rec->input[rec->length], 0, i);
            rec->length += i;
            rec->input[l - 1] = (unsigned char)(i - 1);
        }

        if (!sending) {
            if (l == 0 || l % bs != 0)
                return 0;
            /* otherwise, rec->length >= bs */
        }

        /* TODO(size_t): Convert this call */
        if (EVP_Cipher(ds, rec->data, rec->input, (unsigned int)l) < 1)
            return -1;

        if (EVP_MD_CTX_md(s->read_hash) != NULL) {
            /* TODO(size_t): convert me */
            imac_size = EVP_MD_CTX_size(s->read_hash);
            if (imac_size < 0) {
                SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_SSL3_ENC,
                         ERR_R_INTERNAL_ERROR);
                return -1;
            }
            mac_size = (size_t)imac_size;
        }
        if ((bs != 1) && !sending)
            return ssl3_cbc_remove_padding(rec, bs, mac_size);
    }
    return 1;
}

#define MAX_PADDING 256
/*-
 * tls1_enc encrypts/decrypts |n_recs| in |recs|.  Will call SSLfatal() for
 * internal errors, but not otherwise.
 *
 * Returns:
 *   0: (in non-constant time) if the record is publically invalid (i.e. too
 *       short etc).
 *   1: if the record's padding is valid / the encryption was successful.
 *   -1: if the record's padding/AEAD-authenticator is invalid or, if sending,
 *       an internal error occurred.
 */
int tls1_enc(SSL *s, SSL3_RECORD *recs, size_t n_recs, int sending)
{
    EVP_CIPHER_CTX *ds;
    size_t reclen[SSL_MAX_PIPELINES];
    unsigned char buf[SSL_MAX_PIPELINES][EVP_AEAD_TLS1_AAD_LEN];
    int i, pad = 0, ret, tmpr;
    size_t bs, mac_size = 0, ctr, padnum, loop;
    unsigned char padval;
    int imac_size;
    const EVP_CIPHER *enc;

    if (n_recs == 0) {
        SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_TLS1_ENC,
                 ERR_R_INTERNAL_ERROR);
        return 0;
    }

    if (sending) {
        if (EVP_MD_CTX_md(s->write_hash)) {
            int n = EVP_MD_CTX_size(s->write_hash);
            if (!ossl_assert(n >= 0)) {
                SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_TLS1_ENC,
                         ERR_R_INTERNAL_ERROR);
                return -1;
            }
        }
        ds = s->enc_write_ctx;
        if (s->enc_write_ctx == NULL)
            enc = NULL;
        else {
            int ivlen;
            enc = EVP_CIPHER_CTX_cipher(s->enc_write_ctx);
            /* For TLSv1.1 and later explicit IV */
            if (SSL_USE_EXPLICIT_IV(s)
                && EVP_CIPHER_mode(enc) == EVP_CIPH_CBC_MODE)
                ivlen = EVP_CIPHER_iv_length(enc);
            else
                ivlen = 0;
            if (ivlen > 1) {
                for (ctr = 0; ctr < n_recs; ctr++) {
                    if (recs[ctr].data != recs[ctr].input) {
                        /*
                         * we can't write into the input stream: Can this ever
                         * happen?? (steve)
                         */
                        SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_TLS1_ENC,
                                 ERR_R_INTERNAL_ERROR);
                        return -1;
                    } else if (RAND_bytes(recs[ctr].input, ivlen) <= 0) {
                        SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_TLS1_ENC,
                                 ERR_R_INTERNAL_ERROR);
                        return -1;
                    }
                }
            }
        }
    } else {
        if (EVP_MD_CTX_md(s->read_hash)) {
            int n = EVP_MD_CTX_size(s->read_hash);
            if (!ossl_assert(n >= 0)) {
                SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_TLS1_ENC,
                         ERR_R_INTERNAL_ERROR);
                return -1;
            }
        }
        ds = s->enc_read_ctx;
        if (s->enc_read_ctx == NULL)
            enc = NULL;
        else
            enc = EVP_CIPHER_CTX_cipher(s->enc_read_ctx);
    }

    if ((s->session == NULL) || (ds == NULL) || (enc == NULL)) {
        for (ctr = 0; ctr < n_recs; ctr++) {
            memmove(recs[ctr].data, recs[ctr].input, recs[ctr].length);
            recs[ctr].input = recs[ctr].data;
        }
        ret = 1;
    } else {
        bs = EVP_CIPHER_block_size(EVP_CIPHER_CTX_cipher(ds));

        if (n_recs > 1) {
            if (!(EVP_CIPHER_flags(EVP_CIPHER_CTX_cipher(ds))
                  & EVP_CIPH_FLAG_PIPELINE)) {
                /*
                 * We shouldn't have been called with pipeline data if the
                 * cipher doesn't support pipelining
                 */
                SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_TLS1_ENC,
                         SSL_R_PIPELINE_FAILURE);
                return -1;
            }
        }
        for (ctr = 0; ctr < n_recs; ctr++) {
            reclen[ctr] = recs[ctr].length;

            if (EVP_CIPHER_flags(EVP_CIPHER_CTX_cipher(ds))
                & EVP_CIPH_FLAG_AEAD_CIPHER) {
                unsigned char *seq;

                seq = sending ? RECORD_LAYER_get_write_sequence(&s->rlayer)
                    : RECORD_LAYER_get_read_sequence(&s->rlayer);

                if (SSL_IS_DTLS(s)) {
                    /* DTLS does not support pipelining */
                    unsigned char dtlsseq[9], *p = dtlsseq;

                    s2n(sending ? DTLS_RECORD_LAYER_get_w_epoch(&s->rlayer) :
                        DTLS_RECORD_LAYER_get_r_epoch(&s->rlayer), p);
                    memcpy(p, &seq[2], 6);
                    memcpy(buf[ctr], dtlsseq, 8);
                } else {
                    memcpy(buf[ctr], seq, 8);
                    for (i = 7; i >= 0; i--) { /* increment */
                        ++seq[i];
                        if (seq[i] != 0)
                            break;
                    }
                }

                buf[ctr][8] = recs[ctr].type;
                buf[ctr][9] = (unsigned char)(s->version >> 8);
                buf[ctr][10] = (unsigned char)(s->version);
                buf[ctr][11] = (unsigned char)(recs[ctr].length >> 8);
                buf[ctr][12] = (unsigned char)(recs[ctr].length & 0xff);
                pad = EVP_CIPHER_CTX_ctrl(ds, EVP_CTRL_AEAD_TLS1_AAD,
                                          EVP_AEAD_TLS1_AAD_LEN, buf[ctr]);
                if (pad <= 0) {
                    SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_TLS1_ENC,
                             ERR_R_INTERNAL_ERROR);
                    return -1;
                }

                if (sending) {
                    reclen[ctr] += pad;
                    recs[ctr].length += pad;
                }

            } else if ((bs != 1) && sending) {
                padnum = bs - (reclen[ctr] % bs);

                /* Add weird padding of upto 256 bytes */

                if (padnum > MAX_PADDING) {
                    SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_TLS1_ENC,
                             ERR_R_INTERNAL_ERROR);
                    return -1;
                }
                /* we need to add 'padnum' padding bytes of value padval */
                padval = (unsigned char)(padnum - 1);
                for (loop = reclen[ctr]; loop < reclen[ctr] + padnum; loop++)
                    recs[ctr].input[loop] = padval;
                reclen[ctr] += padnum;
                recs[ctr].length += padnum;
            }

            if (!sending) {
                if (reclen[ctr] == 0 || reclen[ctr] % bs != 0)
                    return 0;
            }
        }
        if (n_recs > 1) {
            unsigned char *data[SSL_MAX_PIPELINES];

            /* Set the output buffers */
            for (ctr = 0; ctr < n_recs; ctr++) {
                data[ctr] = recs[ctr].data;
            }
            if (EVP_CIPHER_CTX_ctrl(ds, EVP_CTRL_SET_PIPELINE_OUTPUT_BUFS,
                                    (int)n_recs, data) <= 0) {
                SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_TLS1_ENC,
                         SSL_R_PIPELINE_FAILURE);
                return -1;
            }
            /* Set the input buffers */
            for (ctr = 0; ctr < n_recs; ctr++) {
                data[ctr] = recs[ctr].input;
            }
            if (EVP_CIPHER_CTX_ctrl(ds, EVP_CTRL_SET_PIPELINE_INPUT_BUFS,
                                    (int)n_recs, data) <= 0
                || EVP_CIPHER_CTX_ctrl(ds, EVP_CTRL_SET_PIPELINE_INPUT_LENS,
                                       (int)n_recs, reclen) <= 0) {
                SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_TLS1_ENC,
                         SSL_R_PIPELINE_FAILURE);
                return -1;
            }
        }

        /* TODO(size_t): Convert this call */
        tmpr = EVP_Cipher(ds, recs[0].data, recs[0].input,
                          (unsigned int)reclen[0]);
        if ((EVP_CIPHER_flags(EVP_CIPHER_CTX_cipher(ds))
             & EVP_CIPH_FLAG_CUSTOM_CIPHER)
            ? (tmpr < 0)
            : (tmpr == 0))
            return -1;          /* AEAD can fail to verify MAC */

        if (sending == 0) {
            if (EVP_CIPHER_mode(enc) == EVP_CIPH_GCM_MODE) {
                for (ctr = 0; ctr < n_recs; ctr++) {
                    recs[ctr].data += EVP_GCM_TLS_EXPLICIT_IV_LEN;
                    recs[ctr].input += EVP_GCM_TLS_EXPLICIT_IV_LEN;
                    recs[ctr].length -= EVP_GCM_TLS_EXPLICIT_IV_LEN;
                }
            } else if (EVP_CIPHER_mode(enc) == EVP_CIPH_CCM_MODE) {
                for (ctr = 0; ctr < n_recs; ctr++) {
                    recs[ctr].data += EVP_CCM_TLS_EXPLICIT_IV_LEN;
                    recs[ctr].input += EVP_CCM_TLS_EXPLICIT_IV_LEN;
                    recs[ctr].length -= EVP_CCM_TLS_EXPLICIT_IV_LEN;
                }
            }
        }

        ret = 1;
        if (!SSL_READ_ETM(s) && EVP_MD_CTX_md(s->read_hash) != NULL) {
            imac_size = EVP_MD_CTX_size(s->read_hash);
            if (imac_size < 0) {
                SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_TLS1_ENC,
                         ERR_R_INTERNAL_ERROR);
                return -1;
            }
            mac_size = (size_t)imac_size;
        }
        if ((bs != 1) && !sending) {
            int tmpret;
            for (ctr = 0; ctr < n_recs; ctr++) {
                tmpret = tls1_cbc_remove_padding(s, &recs[ctr], bs, mac_size);
                /*
                 * If tmpret == 0 then this means publicly invalid so we can
                 * short circuit things here. Otherwise we must respect constant
                 * time behaviour.
                 */
                if (tmpret == 0)
                    return 0;
                ret = constant_time_select_int(constant_time_eq_int(tmpret, 1),
                                               ret, -1);
            }
        }
        if (pad && !sending) {
            for (ctr = 0; ctr < n_recs; ctr++) {
                recs[ctr].length -= pad;
            }
        }
    }
    return ret;
}

int n_ssl3_mac(SSL *ssl, SSL3_RECORD *rec, unsigned char *md, int sending)
{
    unsigned char *mac_sec, *seq;
    const EVP_MD_CTX *hash;
    unsigned char *p, rec_char;
    size_t md_size;
    size_t npad;
    int t;

    if (sending) {
        mac_sec = &(ssl->s3->write_mac_secret[0]);
        seq = RECORD_LAYER_get_write_sequence(&ssl->rlayer);
        hash = ssl->write_hash;
    } else {
        mac_sec = &(ssl->s3->read_mac_secret[0]);
        seq = RECORD_LAYER_get_read_sequence(&ssl->rlayer);
        hash = ssl->read_hash;
    }

    t = EVP_MD_CTX_size(hash);
    if (t < 0)
        return 0;
    md_size = t;
    npad = (48 / md_size) * md_size;

    if (!sending &&
        EVP_CIPHER_CTX_mode(ssl->enc_read_ctx) == EVP_CIPH_CBC_MODE &&
        ssl3_cbc_record_digest_supported(hash)) {
        /*
         * This is a CBC-encrypted record. We must avoid leaking any
         * timing-side channel information about how many blocks of data we
         * are hashing because that gives an attacker a timing-oracle.
         */

        /*-
         * npad is, at most, 48 bytes and that's with MD5:
         *   16 + 48 + 8 (sequence bytes) + 1 + 2 = 75.
         *
         * With SHA-1 (the largest hash speced for SSLv3) the hash size
         * goes up 4, but npad goes down by 8, resulting in a smaller
         * total size.
         */
        unsigned char header[75];
        size_t j = 0;
        memcpy(header + j, mac_sec, md_size);
        j += md_size;
        memcpy(header + j, ssl3_pad_1, npad);
        j += npad;
        memcpy(header + j, seq, 8);
        j += 8;
        header[j++] = rec->type;
        header[j++] = (unsigned char)(rec->length >> 8);
        header[j++] = (unsigned char)(rec->length & 0xff);

        /* Final param == is SSLv3 */
        if (ssl3_cbc_digest_record(hash,
                                   md, &md_size,
                                   header, rec->input,
                                   rec->length + md_size, rec->orig_len,
                                   mac_sec, md_size, 1) <= 0)
            return 0;
    } else {
        unsigned int md_size_u;
        /* Chop the digest off the end :-) */
        EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();

        if (md_ctx == NULL)
            return 0;

        rec_char = rec->type;
        p = md;
        s2n(rec->length, p);
        if (EVP_MD_CTX_copy_ex(md_ctx, hash) <= 0
            || EVP_DigestUpdate(md_ctx, mac_sec, md_size) <= 0
            || EVP_DigestUpdate(md_ctx, ssl3_pad_1, npad) <= 0
            || EVP_DigestUpdate(md_ctx, seq, 8) <= 0
            || EVP_DigestUpdate(md_ctx, &rec_char, 1) <= 0
            || EVP_DigestUpdate(md_ctx, md, 2) <= 0
            || EVP_DigestUpdate(md_ctx, rec->input, rec->length) <= 0
            || EVP_DigestFinal_ex(md_ctx, md, NULL) <= 0
            || EVP_MD_CTX_copy_ex(md_ctx, hash) <= 0
            || EVP_DigestUpdate(md_ctx, mac_sec, md_size) <= 0
            || EVP_DigestUpdate(md_ctx, ssl3_pad_2, npad) <= 0
            || EVP_DigestUpdate(md_ctx, md, md_size) <= 0
            || EVP_DigestFinal_ex(md_ctx, md, &md_size_u) <= 0) {
            EVP_MD_CTX_free(md_ctx);
            return 0;
        }

        EVP_MD_CTX_free(md_ctx);
    }

    ssl3_record_sequence_update(seq);
    return 1;
}

int tls1_mac(SSL *ssl, SSL3_RECORD *rec, unsigned char *md, int sending)
{
    unsigned char *seq;
    EVP_MD_CTX *hash;
    size_t md_size;
    int i;
    EVP_MD_CTX *hmac = NULL, *mac_ctx;
    unsigned char header[13];
    int stream_mac = (sending ? (ssl->mac_flags & SSL_MAC_FLAG_WRITE_MAC_STREAM)
                      : (ssl->mac_flags & SSL_MAC_FLAG_READ_MAC_STREAM));
    int t;

    if (sending) {
        seq = RECORD_LAYER_get_write_sequence(&ssl->rlayer);
        hash = ssl->write_hash;
    } else {
        seq = RECORD_LAYER_get_read_sequence(&ssl->rlayer);
        hash = ssl->read_hash;
    }

    t = EVP_MD_CTX_size(hash);
    if (!ossl_assert(t >= 0))
        return 0;
    md_size = t;

    /* I should fix this up TLS TLS TLS TLS TLS XXXXXXXX */
    if (stream_mac) {
        mac_ctx = hash;
    } else {
        hmac = EVP_MD_CTX_new();
        if (hmac == NULL || !EVP_MD_CTX_copy(hmac, hash)) {
            EVP_MD_CTX_free(hmac);
            return 0;
        }
        mac_ctx = hmac;
    }

    if (SSL_IS_DTLS(ssl)) {
        unsigned char dtlsseq[8], *p = dtlsseq;

        s2n(sending ? DTLS_RECORD_LAYER_get_w_epoch(&ssl->rlayer) :
            DTLS_RECORD_LAYER_get_r_epoch(&ssl->rlayer), p);
        memcpy(p, &seq[2], 6);

        memcpy(header, dtlsseq, 8);
    } else
        memcpy(header, seq, 8);

    header[8] = rec->type;
    header[9] = (unsigned char)(ssl->version >> 8);
    header[10] = (unsigned char)(ssl->version);
    header[11] = (unsigned char)(rec->length >> 8);
    header[12] = (unsigned char)(rec->length & 0xff);

    if (!sending && !SSL_READ_ETM(ssl) &&
        EVP_CIPHER_CTX_mode(ssl->enc_read_ctx) == EVP_CIPH_CBC_MODE &&
        ssl3_cbc_record_digest_supported(mac_ctx)) {
        /*
         * This is a CBC-encrypted record. We must avoid leaking any
         * timing-side channel information about how many blocks of data we
         * are hashing because that gives an attacker a timing-oracle.
         */
        /* Final param == not SSLv3 */
        if (ssl3_cbc_digest_record(mac_ctx,
                                   md, &md_size,
                                   header, rec->input,
                                   rec->length + md_size, rec->orig_len,
                                   ssl->s3->read_mac_secret,
                                   ssl->s3->read_mac_secret_size, 0) <= 0) {
            EVP_MD_CTX_free(hmac);
            return 0;
        }
    } else {
        /* TODO(size_t): Convert these calls */
        if (EVP_DigestSignUpdate(mac_ctx, header, sizeof(header)) <= 0
            || EVP_DigestSignUpdate(mac_ctx, rec->input, rec->length) <= 0
            || EVP_DigestSignFinal(mac_ctx, md, &md_size) <= 0) {
            EVP_MD_CTX_free(hmac);
            return 0;
        }
    }

    EVP_MD_CTX_free(hmac);

#ifdef SSL_DEBUG
    fprintf(stderr, "seq=");
    {
        int z;
        for (z = 0; z < 8; z++)
            fprintf(stderr, "%02X ", seq[z]);
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "rec=");
    {
        size_t z;
        for (z = 0; z < rec->length; z++)
            fprintf(stderr, "%02X ", rec->data[z]);
        fprintf(stderr, "\n");
    }
#endif

    if (!SSL_IS_DTLS(ssl)) {
        for (i = 7; i >= 0; i--) {
            ++seq[i];
            if (seq[i] != 0)
                break;
        }
    }
#ifdef SSL_DEBUG
    {
        unsigned int z;
        for (z = 0; z < md_size; z++)
            fprintf(stderr, "%02X ", md[z]);
        fprintf(stderr, "\n");
    }
#endif
    return 1;
}

/*-
 * ssl3_cbc_remove_padding removes padding from the decrypted, SSLv3, CBC
 * record in |rec| by updating |rec->length| in constant time.
 *
 * block_size: the block size of the cipher used to encrypt the record.
 * returns:
 *   0: (in non-constant time) if the record is publicly invalid.
 *   1: if the padding was valid
 *  -1: otherwise.
 */
int ssl3_cbc_remove_padding(SSL3_RECORD *rec,
                            size_t block_size, size_t mac_size)
{
    size_t padding_length;
    size_t good;
    const size_t overhead = 1 /* padding length byte */  + mac_size;

    /*
     * These lengths are all public so we can test them in non-constant time.
     */
    if (overhead > rec->length)
        return 0;

    padding_length = rec->data[rec->length - 1];
    good = constant_time_ge_s(rec->length, padding_length + overhead);
    /* SSLv3 requires that the padding is minimal. */
    good &= constant_time_ge_s(block_size, padding_length + 1);
    rec->length -= good & (padding_length + 1);
    return constant_time_select_int_s(good, 1, -1);
}

/*-
 * tls1_cbc_remove_padding removes the CBC padding from the decrypted, TLS, CBC
 * record in |rec| in constant time and returns 1 if the padding is valid and
 * -1 otherwise. It also removes any explicit IV from the start of the record
 * without leaking any timing about whether there was enough space after the
 * padding was removed.
 *
 * block_size: the block size of the cipher used to encrypt the record.
 * returns:
 *   0: (in non-constant time) if the record is publicly invalid.
 *   1: if the padding was valid
 *  -1: otherwise.
 */
int tls1_cbc_remove_padding(const SSL *s,
                            SSL3_RECORD *rec,
                            size_t block_size, size_t mac_size)
{
    size_t good;
    size_t padding_length, to_check, i;
    const size_t overhead = 1 /* padding length byte */  + mac_size;
    /* Check if version requires explicit IV */
    if (SSL_USE_EXPLICIT_IV(s)) {
        /*
         * These lengths are all public so we can test them in non-constant
         * time.
         */
        if (overhead + block_size > rec->length)
            return 0;
        /* We can now safely skip explicit IV */
        rec->data += block_size;
        rec->input += block_size;
        rec->length -= block_size;
        rec->orig_len -= block_size;
    } else if (overhead > rec->length)
        return 0;

    padding_length = rec->data[rec->length - 1];

    if (EVP_CIPHER_flags(EVP_CIPHER_CTX_cipher(s->enc_read_ctx)) &
        EVP_CIPH_FLAG_AEAD_CIPHER) {
        /* padding is already verified */
        rec->length -= padding_length + 1;
        return 1;
    }

    good = constant_time_ge_s(rec->length, overhead + padding_length);
    /*
     * The padding consists of a length byte at the end of the record and
     * then that many bytes of padding, all with the same value as the length
     * byte. Thus, with the length byte included, there are i+1 bytes of
     * padding. We can't check just |padding_length+1| bytes because that
     * leaks decrypted information. Therefore we always have to check the
     * maximum amount of padding possible. (Again, the length of the record
     * is public information so we can use it.)
     */
    to_check = 256;            /* maximum amount of padding, inc length byte. */
    if (to_check > rec->length)
        to_check = rec->length;

    for (i = 0; i < to_check; i++) {
        unsigned char mask = constant_time_ge_8_s(padding_length, i);
        unsigned char b = rec->data[rec->length - 1 - i];
        /*
         * The final |padding_length+1| bytes should all have the value
         * |padding_length|. Therefore the XOR should be zero.
         */
        good &= ~(mask & (padding_length ^ b));
    }

    /*
     * If any of the final |padding_length+1| bytes had the wrong value, one
     * or more of the lower eight bits of |good| will be cleared.
     */
    good = constant_time_eq_s(0xff, good & 0xff);
    rec->length -= good & (padding_length + 1);

    return constant_time_select_int_s(good, 1, -1);
}

/*-
 * ssl3_cbc_copy_mac copies |md_size| bytes from the end of |rec| to |out| in
 * constant time (independent of the concrete value of rec->length, which may
 * vary within a 256-byte window).
 *
 * ssl3_cbc_remove_padding or tls1_cbc_remove_padding must be called prior to
 * this function.
 *
 * On entry:
 *   rec->orig_len >= md_size
 *   md_size <= EVP_MAX_MD_SIZE
 *
 * If CBC_MAC_ROTATE_IN_PLACE is defined then the rotation is performed with
 * variable accesses in a 64-byte-aligned buffer. Assuming that this fits into
 * a single or pair of cache-lines, then the variable memory accesses don't
 * actually affect the timing. CPUs with smaller cache-lines [if any] are
 * not multi-core and are not considered vulnerable to cache-timing attacks.
 */
#define CBC_MAC_ROTATE_IN_PLACE

int ssl3_cbc_copy_mac(unsigned char *out,
                       const SSL3_RECORD *rec, size_t md_size)
{
#if defined(CBC_MAC_ROTATE_IN_PLACE)
    unsigned char rotated_mac_buf[64 + EVP_MAX_MD_SIZE];
    unsigned char *rotated_mac;
#else
    unsigned char rotated_mac[EVP_MAX_MD_SIZE];
#endif

    /*
     * mac_end is the index of |rec->data| just after the end of the MAC.
     */
    size_t mac_end = rec->length;
    size_t mac_start = mac_end - md_size;
    size_t in_mac;
    /*
     * scan_start contains the number of bytes that we can ignore because the
     * MAC's position can only vary by 255 bytes.
     */
    size_t scan_start = 0;
    size_t i, j;
    size_t rotate_offset;

    if (!ossl_assert(rec->orig_len >= md_size
                     && md_size <= EVP_MAX_MD_SIZE))
        return 0;

#if defined(CBC_MAC_ROTATE_IN_PLACE)
    rotated_mac = rotated_mac_buf + ((0 - (size_t)rotated_mac_buf) & 63);
#endif

    /* This information is public so it's safe to branch based on it. */
    if (rec->orig_len > md_size + 255 + 1)
        scan_start = rec->orig_len - (md_size + 255 + 1);

    in_mac = 0;
    rotate_offset = 0;
    memset(rotated_mac, 0, md_size);
    for (i = scan_start, j = 0; i < rec->orig_len; i++) {
        size_t mac_started = constant_time_eq_s(i, mac_start);
        size_t mac_ended = constant_time_lt_s(i, mac_end);
        unsigned char b = rec->data[i];

        in_mac |= mac_started;
        in_mac &= mac_ended;
        rotate_offset |= j & mac_started;
        rotated_mac[j++] |= b & in_mac;
        j &= constant_time_lt_s(j, md_size);
    }

    /* Now rotate the MAC */
#if defined(CBC_MAC_ROTATE_IN_PLACE)
    j = 0;
    for (i = 0; i < md_size; i++) {
        /* in case cache-line is 32 bytes, touch second line */
        ((volatile unsigned char *)rotated_mac)[rotate_offset ^ 32];
        out[j++] = rotated_mac[rotate_offset++];
        rotate_offset &= constant_time_lt_s(rotate_offset, md_size);
    }
#else
    memset(out, 0, md_size);
    rotate_offset = md_size - rotate_offset;
    rotate_offset &= constant_time_lt_s(rotate_offset, md_size);
    for (i = 0; i < md_size; i++) {
        for (j = 0; j < md_size; j++)
            out[j] |= rotated_mac[i] & constant_time_eq_8_s(j, rotate_offset);
        rotate_offset++;
        rotate_offset &= constant_time_lt_s(rotate_offset, md_size);
    }
#endif

    return 1;
}

int dtls1_process_record(SSL *s, DTLS1_BITMAP *bitmap)
{
    int i;
    int enc_err;
    SSL_SESSION *sess;
    SSL3_RECORD *rr;
    int imac_size;
    size_t mac_size;
    unsigned char md[EVP_MAX_MD_SIZE];

    rr = RECORD_LAYER_get_rrec(&s->rlayer);
    sess = s->session;

    /*
     * At this point, s->packet_length == SSL3_RT_HEADER_LNGTH + rr->length,
     * and we have that many bytes in s->packet
     */
    rr->input = &(RECORD_LAYER_get_packet(&s->rlayer)[DTLS1_RT_HEADER_LENGTH]);

    /*
     * ok, we can now read from 's->packet' data into 'rr' rr->input points
     * at rr->length bytes, which need to be copied into rr->data by either
     * the decryption or by the decompression When the data is 'copied' into
     * the rr->data buffer, rr->input will be pointed at the new buffer
     */

    /*
     * We now have - encrypted [ MAC [ compressed [ plain ] ] ] rr->length
     * bytes of encrypted compressed stuff.
     */

    /* check is not needed I believe */
    if (rr->length > SSL3_RT_MAX_ENCRYPTED_LENGTH) {
        SSLfatal(s, SSL_AD_RECORD_OVERFLOW, SSL_F_DTLS1_PROCESS_RECORD,
                 SSL_R_ENCRYPTED_LENGTH_TOO_LONG);
        return 0;
    }

    /* decrypt in place in 'rr->input' */
    rr->data = rr->input;
    rr->orig_len = rr->length;

    if (SSL_READ_ETM(s) && s->read_hash) {
        unsigned char *mac;
        mac_size = EVP_MD_CTX_size(s->read_hash);
        if (!ossl_assert(mac_size <= EVP_MAX_MD_SIZE)) {
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DTLS1_PROCESS_RECORD,
                     ERR_R_INTERNAL_ERROR);
            return 0;
        }
        if (rr->orig_len < mac_size) {
            SSLfatal(s, SSL_AD_DECODE_ERROR, SSL_F_DTLS1_PROCESS_RECORD,
                     SSL_R_LENGTH_TOO_SHORT);
            return 0;
        }
        rr->length -= mac_size;
        mac = rr->data + rr->length;
        i = s->method->ssl3_enc->mac(s, rr, md, 0 /* not send */ );
        if (i == 0 || CRYPTO_memcmp(md, mac, (size_t)mac_size) != 0) {
            SSLfatal(s, SSL_AD_BAD_RECORD_MAC, SSL_F_DTLS1_PROCESS_RECORD,
                   SSL_R_DECRYPTION_FAILED_OR_BAD_RECORD_MAC);
            return 0;
        }
    }

    enc_err = s->method->ssl3_enc->enc(s, rr, 1, 0);
    /*-
     * enc_err is:
     *    0: (in non-constant time) if the record is publically invalid.
     *    1: if the padding is valid
     *   -1: if the padding is invalid
     */
    if (enc_err == 0) {
        if (ossl_statem_in_error(s)) {
            /* SSLfatal() got called */
            return 0;
        }
        /* For DTLS we simply ignore bad packets. */
        rr->length = 0;
        RECORD_LAYER_reset_packet_length(&s->rlayer);
        return 0;
    }
#ifdef SSL_DEBUG
    printf("dec %ld\n", rr->length);
    {
        size_t z;
        for (z = 0; z < rr->length; z++)
            printf("%02X%c", rr->data[z], ((z + 1) % 16) ? ' ' : '\n');
    }
    printf("\n");
#endif

    /* r->length is now the compressed data plus mac */
    if ((sess != NULL) && !SSL_READ_ETM(s) &&
        (s->enc_read_ctx != NULL) && (EVP_MD_CTX_md(s->read_hash) != NULL)) {
        /* s->read_hash != NULL => mac_size != -1 */
        unsigned char *mac = NULL;
        unsigned char mac_tmp[EVP_MAX_MD_SIZE];

        /* TODO(size_t): Convert this to do size_t properly */
        imac_size = EVP_MD_CTX_size(s->read_hash);
        if (imac_size < 0) {
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DTLS1_PROCESS_RECORD,
                     ERR_LIB_EVP);
            return 0;
        }
        mac_size = (size_t)imac_size;
        if (!ossl_assert(mac_size <= EVP_MAX_MD_SIZE)) {
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DTLS1_PROCESS_RECORD,
                     ERR_R_INTERNAL_ERROR);
            return 0;
        }

        /*
         * orig_len is the length of the record before any padding was
         * removed. This is public information, as is the MAC in use,
         * therefore we can safely process the record in a different amount
         * of time if it's too short to possibly contain a MAC.
         */
        if (rr->orig_len < mac_size ||
            /* CBC records must have a padding length byte too. */
            (EVP_CIPHER_CTX_mode(s->enc_read_ctx) == EVP_CIPH_CBC_MODE &&
             rr->orig_len < mac_size + 1)) {
            SSLfatal(s, SSL_AD_DECODE_ERROR, SSL_F_DTLS1_PROCESS_RECORD,
                     SSL_R_LENGTH_TOO_SHORT);
            return 0;
        }

        if (EVP_CIPHER_CTX_mode(s->enc_read_ctx) == EVP_CIPH_CBC_MODE) {
            /*
             * We update the length so that the TLS header bytes can be
             * constructed correctly but we need to extract the MAC in
             * constant time from within the record, without leaking the
             * contents of the padding bytes.
             */
            mac = mac_tmp;
            if (!ssl3_cbc_copy_mac(mac_tmp, rr, mac_size)) {
                SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DTLS1_PROCESS_RECORD,
                         ERR_R_INTERNAL_ERROR);
                return 0;
            }
            rr->length -= mac_size;
        } else {
            /*
             * In this case there's no padding, so |rec->orig_len| equals
             * |rec->length| and we checked that there's enough bytes for
             * |mac_size| above.
             */
            rr->length -= mac_size;
            mac = &rr->data[rr->length];
        }

        i = s->method->ssl3_enc->mac(s, rr, md, 0 /* not send */ );
        if (i == 0 || mac == NULL
            || CRYPTO_memcmp(md, mac, mac_size) != 0)
            enc_err = -1;
        if (rr->length > SSL3_RT_MAX_COMPRESSED_LENGTH + mac_size)
            enc_err = -1;
    }

    if (enc_err < 0) {
        /* decryption failed, silently discard message */
        rr->length = 0;
        RECORD_LAYER_reset_packet_length(&s->rlayer);
        return 0;
    }

    /* r->length is now just compressed */
    if (s->expand != NULL) {
        if (rr->length > SSL3_RT_MAX_COMPRESSED_LENGTH) {
            SSLfatal(s, SSL_AD_RECORD_OVERFLOW, SSL_F_DTLS1_PROCESS_RECORD,
                     SSL_R_COMPRESSED_LENGTH_TOO_LONG);
            return 0;
        }
        if (!ssl3_do_uncompress(s, rr)) {
            SSLfatal(s, SSL_AD_DECOMPRESSION_FAILURE,
                     SSL_F_DTLS1_PROCESS_RECORD, SSL_R_BAD_DECOMPRESSION);
            return 0;
        }
    }

    if (rr->length > SSL3_RT_MAX_PLAIN_LENGTH) {
        SSLfatal(s, SSL_AD_RECORD_OVERFLOW, SSL_F_DTLS1_PROCESS_RECORD,
                 SSL_R_DATA_LENGTH_TOO_LONG);
        return 0;
    }

    rr->off = 0;
    /*-
     * So at this point the following is true
     * ssl->s3->rrec.type   is the type of record
     * ssl->s3->rrec.length == number of bytes in record
     * ssl->s3->rrec.off    == offset to first valid byte
     * ssl->s3->rrec.data   == where to take bytes from, increment
     *                         after use :-).
     */

    /* we have pulled in a full packet so zero things */
    RECORD_LAYER_reset_packet_length(&s->rlayer);

    /* Mark receipt of record. */
    dtls1_record_bitmap_update(s, bitmap);

    return 1;
}

/*
 * Retrieve a buffered record that belongs to the current epoch, i.e. processed
 */
#define dtls1_get_processed_record(s) \
                   dtls1_retrieve_buffered_record((s), \
                   &(DTLS_RECORD_LAYER_get_processed_rcds(&s->rlayer)))

/*-
 * Call this to get a new input record.
 * It will return <= 0 if more data is needed, normally due to an error
 * or non-blocking IO.
 * When it finishes, one packet has been decoded and can be found in
 * ssl->s3->rrec.type    - is the type of record
 * ssl->s3->rrec.data,   - data
 * ssl->s3->rrec.length, - number of bytes
 */
/* used only by dtls1_read_bytes */
int dtls1_get_record(SSL *s)
{
    int ssl_major, ssl_minor;
    int rret;
    size_t more, n;
    SSL3_RECORD *rr;
    unsigned char *p = NULL;
    unsigned short version;
    DTLS1_BITMAP *bitmap;
    unsigned int is_next_epoch;

    rr = RECORD_LAYER_get_rrec(&s->rlayer);

 again:
    /*
     * The epoch may have changed.  If so, process all the pending records.
     * This is a non-blocking operation.
     */
    if (!dtls1_process_buffered_records(s)) {
        /* SSLfatal() already called */
        return -1;
    }

    /* if we're renegotiating, then there may be buffered records */
    if (dtls1_get_processed_record(s))
        return 1;

    /* get something from the wire */

    /* check if we have the header */
    if ((RECORD_LAYER_get_rstate(&s->rlayer) != SSL_ST_READ_BODY) ||
        (RECORD_LAYER_get_packet_length(&s->rlayer) < DTLS1_RT_HEADER_LENGTH)) {
        rret = ssl3_read_n(s, DTLS1_RT_HEADER_LENGTH,
                           SSL3_BUFFER_get_len(&s->rlayer.rbuf), 0, 1, &n);
        /* read timeout is handled by dtls1_read_bytes */
        if (rret <= 0) {
            /* SSLfatal() already called if appropriate */
            return rret;         /* error or non-blocking */
        }

        /* this packet contained a partial record, dump it */
        if (RECORD_LAYER_get_packet_length(&s->rlayer) !=
            DTLS1_RT_HEADER_LENGTH) {
            RECORD_LAYER_reset_packet_length(&s->rlayer);
            goto again;
        }

        RECORD_LAYER_set_rstate(&s->rlayer, SSL_ST_READ_BODY);

        p = RECORD_LAYER_get_packet(&s->rlayer);

        if (s->msg_callback)
            s->msg_callback(0, 0, SSL3_RT_HEADER, p, DTLS1_RT_HEADER_LENGTH,
                            s, s->msg_callback_arg);

        /* Pull apart the header into the DTLS1_RECORD */
        rr->type = *(p++);
        ssl_major = *(p++);
        ssl_minor = *(p++);
        version = (ssl_major << 8) | ssl_minor;

        /* sequence number is 64 bits, with top 2 bytes = epoch */
        n2s(p, rr->epoch);

        memcpy(&(RECORD_LAYER_get_read_sequence(&s->rlayer)[2]), p, 6);
        p += 6;

        n2s(p, rr->length);
        rr->read = 0;

        /*
         * Lets check the version. We tolerate alerts that don't have the exact
         * version number (e.g. because of protocol version errors)
         */
        if (!s->first_packet && rr->type != SSL3_RT_ALERT) {
            if (version != s->version) {
                /* unexpected version, silently discard */
                rr->length = 0;
                rr->read = 1;
                RECORD_LAYER_reset_packet_length(&s->rlayer);
                goto again;
            }
        }

        if ((version & 0xff00) != (s->version & 0xff00)) {
            /* wrong version, silently discard record */
            rr->length = 0;
            rr->read = 1;
            RECORD_LAYER_reset_packet_length(&s->rlayer);
            goto again;
        }

        if (rr->length > SSL3_RT_MAX_ENCRYPTED_LENGTH) {
            /* record too long, silently discard it */
            rr->length = 0;
            rr->read = 1;
            RECORD_LAYER_reset_packet_length(&s->rlayer);
            goto again;
        }

        /* If received packet overflows own-client Max Fragment Length setting */
        if (s->session != NULL && USE_MAX_FRAGMENT_LENGTH_EXT(s->session)
                && rr->length > GET_MAX_FRAGMENT_LENGTH(s->session)) {
            /* record too long, silently discard it */
            rr->length = 0;
            rr->read = 1;
            RECORD_LAYER_reset_packet_length(&s->rlayer);
            goto again;
        }

        /* now s->rlayer.rstate == SSL_ST_READ_BODY */
    }

    /* s->rlayer.rstate == SSL_ST_READ_BODY, get and decode the data */

    if (rr->length >
        RECORD_LAYER_get_packet_length(&s->rlayer) - DTLS1_RT_HEADER_LENGTH) {
        /* now s->packet_length == DTLS1_RT_HEADER_LENGTH */
        more = rr->length;
        rret = ssl3_read_n(s, more, more, 1, 1, &n);
        /* this packet contained a partial record, dump it */
        if (rret <= 0 || n != more) {
            if (ossl_statem_in_error(s)) {
                /* ssl3_read_n() called SSLfatal() */
                return -1;
            }
            rr->length = 0;
            rr->read = 1;
            RECORD_LAYER_reset_packet_length(&s->rlayer);
            goto again;
        }

        /*
         * now n == rr->length, and s->packet_length ==
         * DTLS1_RT_HEADER_LENGTH + rr->length
         */
    }
    /* set state for later operations */
    RECORD_LAYER_set_rstate(&s->rlayer, SSL_ST_READ_HEADER);

    /* match epochs.  NULL means the packet is dropped on the floor */
    bitmap = dtls1_get_bitmap(s, rr, &is_next_epoch);
    if (bitmap == NULL) {
        rr->length = 0;
        RECORD_LAYER_reset_packet_length(&s->rlayer); /* dump this record */
        goto again;             /* get another record */
    }
#ifndef OPENSSL_NO_SCTP
    /* Only do replay check if no SCTP bio */
    if (!BIO_dgram_is_sctp(SSL_get_rbio(s))) {
#endif
        /* Check whether this is a repeat, or aged record. */
        /*
         * TODO: Does it make sense to have replay protection in epoch 0 where
         * we have no integrity negotiated yet?
         */
        if (!dtls1_record_replay_check(s, bitmap)) {
            rr->length = 0;
            rr->read = 1;
            RECORD_LAYER_reset_packet_length(&s->rlayer); /* dump this record */
            goto again;         /* get another record */
        }
#ifndef OPENSSL_NO_SCTP
    }
#endif

    /* just read a 0 length packet */
    if (rr->length == 0) {
        rr->read = 1;
        goto again;
    }

    /*
     * If this record is from the next epoch (either HM or ALERT), and a
     * handshake is currently in progress, buffer it since it cannot be
     * processed at this time.
     */
    if (is_next_epoch) {
        if ((SSL_in_init(s) || ossl_statem_get_in_handshake(s))) {
            if (dtls1_buffer_record (s,
                    &(DTLS_RECORD_LAYER_get_unprocessed_rcds(&s->rlayer)),
                    rr->seq_num) < 0) {
                /* SSLfatal() already called */
                return -1;
            }
        }
        rr->length = 0;
        rr->read = 1;
        RECORD_LAYER_reset_packet_length(&s->rlayer);
        goto again;
    }

    if (!dtls1_process_record(s, bitmap)) {
        if (ossl_statem_in_error(s)) {
            /* dtls1_process_record() called SSLfatal */
            return -1;
        }
        rr->length = 0;
        rr->read = 1;
        RECORD_LAYER_reset_packet_length(&s->rlayer); /* dump this record */
        goto again;             /* get another record */
    }

    return 1;

}

int dtls_buffer_listen_record(SSL *s, size_t len, unsigned char *seq, size_t off)
{
    SSL3_RECORD *rr;

    rr = RECORD_LAYER_get_rrec(&s->rlayer);
    memset(rr, 0, sizeof(SSL3_RECORD));

    rr->length = len;
    rr->type = SSL3_RT_HANDSHAKE;
    memcpy(rr->seq_num, seq, sizeof(rr->seq_num));
    rr->off = off;

    s->rlayer.packet = RECORD_LAYER_get_rbuf(&s->rlayer)->buf;
    s->rlayer.packet_length = DTLS1_RT_HEADER_LENGTH + len;
    rr->data = s->rlayer.packet + DTLS1_RT_HEADER_LENGTH;

    if (dtls1_buffer_record(s, &(s->rlayer.d->processed_rcds),
                            SSL3_RECORD_get_seq_num(s->rlayer.rrec)) <= 0) {
        /* SSLfatal() already called */
        return 0;
    }

    return 1;
}
