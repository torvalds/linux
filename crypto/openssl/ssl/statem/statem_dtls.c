/*
 * Copyright 2005-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <limits.h>
#include <string.h>
#include <stdio.h>
#include "../ssl_locl.h"
#include "statem_locl.h"
#include "internal/cryptlib.h"
#include <openssl/buffer.h>
#include <openssl/objects.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#define RSMBLY_BITMASK_SIZE(msg_len) (((msg_len) + 7) / 8)

#define RSMBLY_BITMASK_MARK(bitmask, start, end) { \
                        if ((end) - (start) <= 8) { \
                                long ii; \
                                for (ii = (start); ii < (end); ii++) bitmask[((ii) >> 3)] |= (1 << ((ii) & 7)); \
                        } else { \
                                long ii; \
                                bitmask[((start) >> 3)] |= bitmask_start_values[((start) & 7)]; \
                                for (ii = (((start) >> 3) + 1); ii < ((((end) - 1)) >> 3); ii++) bitmask[ii] = 0xff; \
                                bitmask[(((end) - 1) >> 3)] |= bitmask_end_values[((end) & 7)]; \
                        } }

#define RSMBLY_BITMASK_IS_COMPLETE(bitmask, msg_len, is_complete) { \
                        long ii; \
                        is_complete = 1; \
                        if (bitmask[(((msg_len) - 1) >> 3)] != bitmask_end_values[((msg_len) & 7)]) is_complete = 0; \
                        if (is_complete) for (ii = (((msg_len) - 1) >> 3) - 1; ii >= 0 ; ii--) \
                                if (bitmask[ii] != 0xff) { is_complete = 0; break; } }

static unsigned char bitmask_start_values[] =
    { 0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80 };
static unsigned char bitmask_end_values[] =
    { 0xff, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f };

static void dtls1_fix_message_header(SSL *s, size_t frag_off,
                                     size_t frag_len);
static unsigned char *dtls1_write_message_header(SSL *s, unsigned char *p);
static void dtls1_set_message_header_int(SSL *s, unsigned char mt,
                                         size_t len,
                                         unsigned short seq_num,
                                         size_t frag_off,
                                         size_t frag_len);
static int dtls_get_reassembled_message(SSL *s, int *errtype, size_t *len);

static hm_fragment *dtls1_hm_fragment_new(size_t frag_len, int reassembly)
{
    hm_fragment *frag = NULL;
    unsigned char *buf = NULL;
    unsigned char *bitmask = NULL;

    if ((frag = OPENSSL_malloc(sizeof(*frag))) == NULL) {
        SSLerr(SSL_F_DTLS1_HM_FRAGMENT_NEW, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    if (frag_len) {
        if ((buf = OPENSSL_malloc(frag_len)) == NULL) {
            SSLerr(SSL_F_DTLS1_HM_FRAGMENT_NEW, ERR_R_MALLOC_FAILURE);
            OPENSSL_free(frag);
            return NULL;
        }
    }

    /* zero length fragment gets zero frag->fragment */
    frag->fragment = buf;

    /* Initialize reassembly bitmask if necessary */
    if (reassembly) {
        bitmask = OPENSSL_zalloc(RSMBLY_BITMASK_SIZE(frag_len));
        if (bitmask == NULL) {
            SSLerr(SSL_F_DTLS1_HM_FRAGMENT_NEW, ERR_R_MALLOC_FAILURE);
            OPENSSL_free(buf);
            OPENSSL_free(frag);
            return NULL;
        }
    }

    frag->reassembly = bitmask;

    return frag;
}

void dtls1_hm_fragment_free(hm_fragment *frag)
{
    if (!frag)
        return;
    if (frag->msg_header.is_ccs) {
        EVP_CIPHER_CTX_free(frag->msg_header.
                            saved_retransmit_state.enc_write_ctx);
        EVP_MD_CTX_free(frag->msg_header.saved_retransmit_state.write_hash);
    }
    OPENSSL_free(frag->fragment);
    OPENSSL_free(frag->reassembly);
    OPENSSL_free(frag);
}

/*
 * send s->init_buf in records of type 'type' (SSL3_RT_HANDSHAKE or
 * SSL3_RT_CHANGE_CIPHER_SPEC)
 */
int dtls1_do_write(SSL *s, int type)
{
    int ret;
    size_t written;
    size_t curr_mtu;
    int retry = 1;
    size_t len, frag_off, mac_size, blocksize, used_len;

    if (!dtls1_query_mtu(s))
        return -1;

    if (s->d1->mtu < dtls1_min_mtu(s))
        /* should have something reasonable now */
        return -1;

    if (s->init_off == 0 && type == SSL3_RT_HANDSHAKE) {
        if (!ossl_assert(s->init_num ==
                         s->d1->w_msg_hdr.msg_len + DTLS1_HM_HEADER_LENGTH))
            return -1;
    }

    if (s->write_hash) {
        if (s->enc_write_ctx
            && (EVP_CIPHER_flags(EVP_CIPHER_CTX_cipher(s->enc_write_ctx)) &
                EVP_CIPH_FLAG_AEAD_CIPHER) != 0)
            mac_size = 0;
        else
            mac_size = EVP_MD_CTX_size(s->write_hash);
    } else
        mac_size = 0;

    if (s->enc_write_ctx &&
        (EVP_CIPHER_CTX_mode(s->enc_write_ctx) == EVP_CIPH_CBC_MODE))
        blocksize = 2 * EVP_CIPHER_CTX_block_size(s->enc_write_ctx);
    else
        blocksize = 0;

    frag_off = 0;
    s->rwstate = SSL_NOTHING;

    /* s->init_num shouldn't ever be < 0...but just in case */
    while (s->init_num > 0) {
        if (type == SSL3_RT_HANDSHAKE && s->init_off != 0) {
            /* We must be writing a fragment other than the first one */

            if (frag_off > 0) {
                /* This is the first attempt at writing out this fragment */

                if (s->init_off <= DTLS1_HM_HEADER_LENGTH) {
                    /*
                     * Each fragment that was already sent must at least have
                     * contained the message header plus one other byte.
                     * Therefore |init_off| must have progressed by at least
                     * |DTLS1_HM_HEADER_LENGTH + 1| bytes. If not something went
                     * wrong.
                     */
                    return -1;
                }

                /*
                 * Adjust |init_off| and |init_num| to allow room for a new
                 * message header for this fragment.
                 */
                s->init_off -= DTLS1_HM_HEADER_LENGTH;
                s->init_num += DTLS1_HM_HEADER_LENGTH;
            } else {
                /*
                 * We must have been called again after a retry so use the
                 * fragment offset from our last attempt. We do not need
                 * to adjust |init_off| and |init_num| as above, because
                 * that should already have been done before the retry.
                 */
                frag_off = s->d1->w_msg_hdr.frag_off;
            }
        }

        used_len = BIO_wpending(s->wbio) + DTLS1_RT_HEADER_LENGTH
            + mac_size + blocksize;
        if (s->d1->mtu > used_len)
            curr_mtu = s->d1->mtu - used_len;
        else
            curr_mtu = 0;

        if (curr_mtu <= DTLS1_HM_HEADER_LENGTH) {
            /*
             * grr.. we could get an error if MTU picked was wrong
             */
            ret = BIO_flush(s->wbio);
            if (ret <= 0) {
                s->rwstate = SSL_WRITING;
                return ret;
            }
            used_len = DTLS1_RT_HEADER_LENGTH + mac_size + blocksize;
            if (s->d1->mtu > used_len + DTLS1_HM_HEADER_LENGTH) {
                curr_mtu = s->d1->mtu - used_len;
            } else {
                /* Shouldn't happen */
                return -1;
            }
        }

        /*
         * We just checked that s->init_num > 0 so this cast should be safe
         */
        if (((unsigned int)s->init_num) > curr_mtu)
            len = curr_mtu;
        else
            len = s->init_num;

        if (len > s->max_send_fragment)
            len = s->max_send_fragment;

        /*
         * XDTLS: this function is too long.  split out the CCS part
         */
        if (type == SSL3_RT_HANDSHAKE) {
            if (len < DTLS1_HM_HEADER_LENGTH) {
                /*
                 * len is so small that we really can't do anything sensible
                 * so fail
                 */
                return -1;
            }
            dtls1_fix_message_header(s, frag_off, len - DTLS1_HM_HEADER_LENGTH);

            dtls1_write_message_header(s,
                                       (unsigned char *)&s->init_buf->
                                       data[s->init_off]);
        }

        ret = dtls1_write_bytes(s, type, &s->init_buf->data[s->init_off], len,
                                &written);
        if (ret < 0) {
            /*
             * might need to update MTU here, but we don't know which
             * previous packet caused the failure -- so can't really
             * retransmit anything.  continue as if everything is fine and
             * wait for an alert to handle the retransmit
             */
            if (retry && BIO_ctrl(SSL_get_wbio(s),
                                  BIO_CTRL_DGRAM_MTU_EXCEEDED, 0, NULL) > 0) {
                if (!(SSL_get_options(s) & SSL_OP_NO_QUERY_MTU)) {
                    if (!dtls1_query_mtu(s))
                        return -1;
                    /* Have one more go */
                    retry = 0;
                } else
                    return -1;
            } else {
                return -1;
            }
        } else {

            /*
             * bad if this assert fails, only part of the handshake message
             * got sent.  but why would this happen?
             */
            if (!ossl_assert(len == written))
                return -1;

            if (type == SSL3_RT_HANDSHAKE && !s->d1->retransmitting) {
                /*
                 * should not be done for 'Hello Request's, but in that case
                 * we'll ignore the result anyway
                 */
                unsigned char *p =
                    (unsigned char *)&s->init_buf->data[s->init_off];
                const struct hm_header_st *msg_hdr = &s->d1->w_msg_hdr;
                size_t xlen;

                if (frag_off == 0 && s->version != DTLS1_BAD_VER) {
                    /*
                     * reconstruct message header is if it is being sent in
                     * single fragment
                     */
                    *p++ = msg_hdr->type;
                    l2n3(msg_hdr->msg_len, p);
                    s2n(msg_hdr->seq, p);
                    l2n3(0, p);
                    l2n3(msg_hdr->msg_len, p);
                    p -= DTLS1_HM_HEADER_LENGTH;
                    xlen = written;
                } else {
                    p += DTLS1_HM_HEADER_LENGTH;
                    xlen = written - DTLS1_HM_HEADER_LENGTH;
                }

                if (!ssl3_finish_mac(s, p, xlen))
                    return -1;
            }

            if (written == s->init_num) {
                if (s->msg_callback)
                    s->msg_callback(1, s->version, type, s->init_buf->data,
                                    (size_t)(s->init_off + s->init_num), s,
                                    s->msg_callback_arg);

                s->init_off = 0; /* done writing this message */
                s->init_num = 0;

                return 1;
            }
            s->init_off += written;
            s->init_num -= written;
            written -= DTLS1_HM_HEADER_LENGTH;
            frag_off += written;

            /*
             * We save the fragment offset for the next fragment so we have it
             * available in case of an IO retry. We don't know the length of the
             * next fragment yet so just set that to 0 for now. It will be
             * updated again later.
             */
            dtls1_fix_message_header(s, frag_off, 0);
        }
    }
    return 0;
}

int dtls_get_message(SSL *s, int *mt, size_t *len)
{
    struct hm_header_st *msg_hdr;
    unsigned char *p;
    size_t msg_len;
    size_t tmplen;
    int errtype;

    msg_hdr = &s->d1->r_msg_hdr;
    memset(msg_hdr, 0, sizeof(*msg_hdr));

 again:
    if (!dtls_get_reassembled_message(s, &errtype, &tmplen)) {
        if (errtype == DTLS1_HM_BAD_FRAGMENT
                || errtype == DTLS1_HM_FRAGMENT_RETRY) {
            /* bad fragment received */
            goto again;
        }
        return 0;
    }

    *mt = s->s3->tmp.message_type;

    p = (unsigned char *)s->init_buf->data;
    *len = s->init_num;

    if (*mt == SSL3_MT_CHANGE_CIPHER_SPEC) {
        if (s->msg_callback) {
            s->msg_callback(0, s->version, SSL3_RT_CHANGE_CIPHER_SPEC,
                            p, 1, s, s->msg_callback_arg);
        }
        /*
         * This isn't a real handshake message so skip the processing below.
         */
        return 1;
    }

    msg_len = msg_hdr->msg_len;

    /* reconstruct message header */
    *(p++) = msg_hdr->type;
    l2n3(msg_len, p);
    s2n(msg_hdr->seq, p);
    l2n3(0, p);
    l2n3(msg_len, p);
    if (s->version != DTLS1_BAD_VER) {
        p -= DTLS1_HM_HEADER_LENGTH;
        msg_len += DTLS1_HM_HEADER_LENGTH;
    }

    /*
     * If receiving Finished, record MAC of prior handshake messages for
     * Finished verification.
     */
    if (*mt == SSL3_MT_FINISHED && !ssl3_take_mac(s)) {
        /* SSLfatal() already called */
        return 0;
    }

    if (!ssl3_finish_mac(s, p, msg_len))
        return 0;
    if (s->msg_callback)
        s->msg_callback(0, s->version, SSL3_RT_HANDSHAKE,
                        p, msg_len, s, s->msg_callback_arg);

    memset(msg_hdr, 0, sizeof(*msg_hdr));

    s->d1->handshake_read_seq++;

    s->init_msg = s->init_buf->data + DTLS1_HM_HEADER_LENGTH;

    return 1;
}

/*
 * dtls1_max_handshake_message_len returns the maximum number of bytes
 * permitted in a DTLS handshake message for |s|. The minimum is 16KB, but
 * may be greater if the maximum certificate list size requires it.
 */
static size_t dtls1_max_handshake_message_len(const SSL *s)
{
    size_t max_len = DTLS1_HM_HEADER_LENGTH + SSL3_RT_MAX_ENCRYPTED_LENGTH;
    if (max_len < s->max_cert_list)
        return s->max_cert_list;
    return max_len;
}

static int dtls1_preprocess_fragment(SSL *s, struct hm_header_st *msg_hdr)
{
    size_t frag_off, frag_len, msg_len;

    msg_len = msg_hdr->msg_len;
    frag_off = msg_hdr->frag_off;
    frag_len = msg_hdr->frag_len;

    /* sanity checking */
    if ((frag_off + frag_len) > msg_len
            || msg_len > dtls1_max_handshake_message_len(s)) {
        SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER, SSL_F_DTLS1_PREPROCESS_FRAGMENT,
                 SSL_R_EXCESSIVE_MESSAGE_SIZE);
        return 0;
    }

    if (s->d1->r_msg_hdr.frag_off == 0) { /* first fragment */
        /*
         * msg_len is limited to 2^24, but is effectively checked against
         * dtls_max_handshake_message_len(s) above
         */
        if (!BUF_MEM_grow_clean(s->init_buf, msg_len + DTLS1_HM_HEADER_LENGTH)) {
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DTLS1_PREPROCESS_FRAGMENT,
                     ERR_R_BUF_LIB);
            return 0;
        }

        s->s3->tmp.message_size = msg_len;
        s->d1->r_msg_hdr.msg_len = msg_len;
        s->s3->tmp.message_type = msg_hdr->type;
        s->d1->r_msg_hdr.type = msg_hdr->type;
        s->d1->r_msg_hdr.seq = msg_hdr->seq;
    } else if (msg_len != s->d1->r_msg_hdr.msg_len) {
        /*
         * They must be playing with us! BTW, failure to enforce upper limit
         * would open possibility for buffer overrun.
         */
        SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER, SSL_F_DTLS1_PREPROCESS_FRAGMENT,
                 SSL_R_EXCESSIVE_MESSAGE_SIZE);
        return 0;
    }

    return 1;
}

/*
 * Returns 1 if there is a buffered fragment available, 0 if not, or -1 on a
 * fatal error.
 */
static int dtls1_retrieve_buffered_fragment(SSL *s, size_t *len)
{
    /*-
     * (0) check whether the desired fragment is available
     * if so:
     * (1) copy over the fragment to s->init_buf->data[]
     * (2) update s->init_num
     */
    pitem *item;
    hm_fragment *frag;
    int ret;

    do {
        item = pqueue_peek(s->d1->buffered_messages);
        if (item == NULL)
            return 0;

        frag = (hm_fragment *)item->data;

        if (frag->msg_header.seq < s->d1->handshake_read_seq) {
            /* This is a stale message that has been buffered so clear it */
            pqueue_pop(s->d1->buffered_messages);
            dtls1_hm_fragment_free(frag);
            pitem_free(item);
            item = NULL;
            frag = NULL;
        }
    } while (item == NULL);

    /* Don't return if reassembly still in progress */
    if (frag->reassembly != NULL)
        return 0;

    if (s->d1->handshake_read_seq == frag->msg_header.seq) {
        size_t frag_len = frag->msg_header.frag_len;
        pqueue_pop(s->d1->buffered_messages);

        /* Calls SSLfatal() as required */
        ret = dtls1_preprocess_fragment(s, &frag->msg_header);

        if (ret && frag->msg_header.frag_len > 0) {
            unsigned char *p =
                (unsigned char *)s->init_buf->data + DTLS1_HM_HEADER_LENGTH;
            memcpy(&p[frag->msg_header.frag_off], frag->fragment,
                   frag->msg_header.frag_len);
        }

        dtls1_hm_fragment_free(frag);
        pitem_free(item);

        if (ret) {
            *len = frag_len;
            return 1;
        }

        /* Fatal error */
        s->init_num = 0;
        return -1;
    } else {
        return 0;
    }
}

static int
dtls1_reassemble_fragment(SSL *s, const struct hm_header_st *msg_hdr)
{
    hm_fragment *frag = NULL;
    pitem *item = NULL;
    int i = -1, is_complete;
    unsigned char seq64be[8];
    size_t frag_len = msg_hdr->frag_len;
    size_t readbytes;

    if ((msg_hdr->frag_off + frag_len) > msg_hdr->msg_len ||
        msg_hdr->msg_len > dtls1_max_handshake_message_len(s))
        goto err;

    if (frag_len == 0) {
        return DTLS1_HM_FRAGMENT_RETRY;
    }

    /* Try to find item in queue */
    memset(seq64be, 0, sizeof(seq64be));
    seq64be[6] = (unsigned char)(msg_hdr->seq >> 8);
    seq64be[7] = (unsigned char)msg_hdr->seq;
    item = pqueue_find(s->d1->buffered_messages, seq64be);

    if (item == NULL) {
        frag = dtls1_hm_fragment_new(msg_hdr->msg_len, 1);
        if (frag == NULL)
            goto err;
        memcpy(&(frag->msg_header), msg_hdr, sizeof(*msg_hdr));
        frag->msg_header.frag_len = frag->msg_header.msg_len;
        frag->msg_header.frag_off = 0;
    } else {
        frag = (hm_fragment *)item->data;
        if (frag->msg_header.msg_len != msg_hdr->msg_len) {
            item = NULL;
            frag = NULL;
            goto err;
        }
    }

    /*
     * If message is already reassembled, this must be a retransmit and can
     * be dropped. In this case item != NULL and so frag does not need to be
     * freed.
     */
    if (frag->reassembly == NULL) {
        unsigned char devnull[256];

        while (frag_len) {
            i = s->method->ssl_read_bytes(s, SSL3_RT_HANDSHAKE, NULL,
                                          devnull,
                                          frag_len >
                                          sizeof(devnull) ? sizeof(devnull) :
                                          frag_len, 0, &readbytes);
            if (i <= 0)
                goto err;
            frag_len -= readbytes;
        }
        return DTLS1_HM_FRAGMENT_RETRY;
    }

    /* read the body of the fragment (header has already been read */
    i = s->method->ssl_read_bytes(s, SSL3_RT_HANDSHAKE, NULL,
                                  frag->fragment + msg_hdr->frag_off,
                                  frag_len, 0, &readbytes);
    if (i <= 0 || readbytes != frag_len)
        i = -1;
    if (i <= 0)
        goto err;

    RSMBLY_BITMASK_MARK(frag->reassembly, (long)msg_hdr->frag_off,
                        (long)(msg_hdr->frag_off + frag_len));

    if (!ossl_assert(msg_hdr->msg_len > 0))
        goto err;
    RSMBLY_BITMASK_IS_COMPLETE(frag->reassembly, (long)msg_hdr->msg_len,
                               is_complete);

    if (is_complete) {
        OPENSSL_free(frag->reassembly);
        frag->reassembly = NULL;
    }

    if (item == NULL) {
        item = pitem_new(seq64be, frag);
        if (item == NULL) {
            i = -1;
            goto err;
        }

        item = pqueue_insert(s->d1->buffered_messages, item);
        /*
         * pqueue_insert fails iff a duplicate item is inserted. However,
         * |item| cannot be a duplicate. If it were, |pqueue_find|, above,
         * would have returned it and control would never have reached this
         * branch.
         */
        if (!ossl_assert(item != NULL))
            goto err;
    }

    return DTLS1_HM_FRAGMENT_RETRY;

 err:
    if (item == NULL)
        dtls1_hm_fragment_free(frag);
    return -1;
}

static int
dtls1_process_out_of_seq_message(SSL *s, const struct hm_header_st *msg_hdr)
{
    int i = -1;
    hm_fragment *frag = NULL;
    pitem *item = NULL;
    unsigned char seq64be[8];
    size_t frag_len = msg_hdr->frag_len;
    size_t readbytes;

    if ((msg_hdr->frag_off + frag_len) > msg_hdr->msg_len)
        goto err;

    /* Try to find item in queue, to prevent duplicate entries */
    memset(seq64be, 0, sizeof(seq64be));
    seq64be[6] = (unsigned char)(msg_hdr->seq >> 8);
    seq64be[7] = (unsigned char)msg_hdr->seq;
    item = pqueue_find(s->d1->buffered_messages, seq64be);

    /*
     * If we already have an entry and this one is a fragment, don't discard
     * it and rather try to reassemble it.
     */
    if (item != NULL && frag_len != msg_hdr->msg_len)
        item = NULL;

    /*
     * Discard the message if sequence number was already there, is too far
     * in the future, already in the queue or if we received a FINISHED
     * before the SERVER_HELLO, which then must be a stale retransmit.
     */
    if (msg_hdr->seq <= s->d1->handshake_read_seq ||
        msg_hdr->seq > s->d1->handshake_read_seq + 10 || item != NULL ||
        (s->d1->handshake_read_seq == 0 && msg_hdr->type == SSL3_MT_FINISHED)) {
        unsigned char devnull[256];

        while (frag_len) {
            i = s->method->ssl_read_bytes(s, SSL3_RT_HANDSHAKE, NULL,
                                          devnull,
                                          frag_len >
                                          sizeof(devnull) ? sizeof(devnull) :
                                          frag_len, 0, &readbytes);
            if (i <= 0)
                goto err;
            frag_len -= readbytes;
        }
    } else {
        if (frag_len != msg_hdr->msg_len) {
            return dtls1_reassemble_fragment(s, msg_hdr);
        }

        if (frag_len > dtls1_max_handshake_message_len(s))
            goto err;

        frag = dtls1_hm_fragment_new(frag_len, 0);
        if (frag == NULL)
            goto err;

        memcpy(&(frag->msg_header), msg_hdr, sizeof(*msg_hdr));

        if (frag_len) {
            /*
             * read the body of the fragment (header has already been read
             */
            i = s->method->ssl_read_bytes(s, SSL3_RT_HANDSHAKE, NULL,
                                          frag->fragment, frag_len, 0,
                                          &readbytes);
            if (i<=0 || readbytes != frag_len)
                i = -1;
            if (i <= 0)
                goto err;
        }

        item = pitem_new(seq64be, frag);
        if (item == NULL)
            goto err;

        item = pqueue_insert(s->d1->buffered_messages, item);
        /*
         * pqueue_insert fails iff a duplicate item is inserted. However,
         * |item| cannot be a duplicate. If it were, |pqueue_find|, above,
         * would have returned it. Then, either |frag_len| !=
         * |msg_hdr->msg_len| in which case |item| is set to NULL and it will
         * have been processed with |dtls1_reassemble_fragment|, above, or
         * the record will have been discarded.
         */
        if (!ossl_assert(item != NULL))
            goto err;
    }

    return DTLS1_HM_FRAGMENT_RETRY;

 err:
    if (item == NULL)
        dtls1_hm_fragment_free(frag);
    return 0;
}

static int dtls_get_reassembled_message(SSL *s, int *errtype, size_t *len)
{
    unsigned char wire[DTLS1_HM_HEADER_LENGTH];
    size_t mlen, frag_off, frag_len;
    int i, ret, recvd_type;
    struct hm_header_st msg_hdr;
    size_t readbytes;

    *errtype = 0;

 redo:
    /* see if we have the required fragment already */
    ret = dtls1_retrieve_buffered_fragment(s, &frag_len);
    if (ret < 0) {
        /* SSLfatal() already called */
        return 0;
    }
    if (ret > 0) {
        s->init_num = frag_len;
        *len = frag_len;
        return 1;
    }

    /* read handshake message header */
    i = s->method->ssl_read_bytes(s, SSL3_RT_HANDSHAKE, &recvd_type, wire,
                                  DTLS1_HM_HEADER_LENGTH, 0, &readbytes);
    if (i <= 0) {               /* nbio, or an error */
        s->rwstate = SSL_READING;
        *len = 0;
        return 0;
    }
    if (recvd_type == SSL3_RT_CHANGE_CIPHER_SPEC) {
        if (wire[0] != SSL3_MT_CCS) {
            SSLfatal(s, SSL_AD_UNEXPECTED_MESSAGE,
                     SSL_F_DTLS_GET_REASSEMBLED_MESSAGE,
                     SSL_R_BAD_CHANGE_CIPHER_SPEC);
            goto f_err;
        }

        memcpy(s->init_buf->data, wire, readbytes);
        s->init_num = readbytes - 1;
        s->init_msg = s->init_buf->data + 1;
        s->s3->tmp.message_type = SSL3_MT_CHANGE_CIPHER_SPEC;
        s->s3->tmp.message_size = readbytes - 1;
        *len = readbytes - 1;
        return 1;
    }

    /* Handshake fails if message header is incomplete */
    if (readbytes != DTLS1_HM_HEADER_LENGTH) {
        SSLfatal(s, SSL_AD_UNEXPECTED_MESSAGE,
                 SSL_F_DTLS_GET_REASSEMBLED_MESSAGE, SSL_R_UNEXPECTED_MESSAGE);
        goto f_err;
    }

    /* parse the message fragment header */
    dtls1_get_message_header(wire, &msg_hdr);

    mlen = msg_hdr.msg_len;
    frag_off = msg_hdr.frag_off;
    frag_len = msg_hdr.frag_len;

    /*
     * We must have at least frag_len bytes left in the record to be read.
     * Fragments must not span records.
     */
    if (frag_len > RECORD_LAYER_get_rrec_length(&s->rlayer)) {
        SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER,
                 SSL_F_DTLS_GET_REASSEMBLED_MESSAGE, SSL_R_BAD_LENGTH);
        goto f_err;
    }

    /*
     * if this is a future (or stale) message it gets buffered
     * (or dropped)--no further processing at this time
     * While listening, we accept seq 1 (ClientHello with cookie)
     * although we're still expecting seq 0 (ClientHello)
     */
    if (msg_hdr.seq != s->d1->handshake_read_seq) {
        *errtype = dtls1_process_out_of_seq_message(s, &msg_hdr);
        return 0;
    }

    if (frag_len && frag_len < mlen) {
        *errtype = dtls1_reassemble_fragment(s, &msg_hdr);
        return 0;
    }

    if (!s->server
            && s->d1->r_msg_hdr.frag_off == 0
            && s->statem.hand_state != TLS_ST_OK
            && wire[0] == SSL3_MT_HELLO_REQUEST) {
        /*
         * The server may always send 'Hello Request' messages -- we are
         * doing a handshake anyway now, so ignore them if their format is
         * correct. Does not count for 'Finished' MAC.
         */
        if (wire[1] == 0 && wire[2] == 0 && wire[3] == 0) {
            if (s->msg_callback)
                s->msg_callback(0, s->version, SSL3_RT_HANDSHAKE,
                                wire, DTLS1_HM_HEADER_LENGTH, s,
                                s->msg_callback_arg);

            s->init_num = 0;
            goto redo;
        } else {                /* Incorrectly formatted Hello request */

            SSLfatal(s, SSL_AD_UNEXPECTED_MESSAGE,
                     SSL_F_DTLS_GET_REASSEMBLED_MESSAGE,
                     SSL_R_UNEXPECTED_MESSAGE);
            goto f_err;
        }
    }

    if (!dtls1_preprocess_fragment(s, &msg_hdr)) {
        /* SSLfatal() already called */
        goto f_err;
    }

    if (frag_len > 0) {
        unsigned char *p =
            (unsigned char *)s->init_buf->data + DTLS1_HM_HEADER_LENGTH;

        i = s->method->ssl_read_bytes(s, SSL3_RT_HANDSHAKE, NULL,
                                      &p[frag_off], frag_len, 0, &readbytes);

        /*
         * This shouldn't ever fail due to NBIO because we already checked
         * that we have enough data in the record
         */
        if (i <= 0) {
            s->rwstate = SSL_READING;
            *len = 0;
            return 0;
        }
    } else {
        readbytes = 0;
    }

    /*
     * XDTLS: an incorrectly formatted fragment should cause the handshake
     * to fail
     */
    if (readbytes != frag_len) {
        SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER,
                 SSL_F_DTLS_GET_REASSEMBLED_MESSAGE, SSL_R_BAD_LENGTH);
        goto f_err;
    }

    /*
     * Note that s->init_num is *not* used as current offset in
     * s->init_buf->data, but as a counter summing up fragments' lengths: as
     * soon as they sum up to handshake packet length, we assume we have got
     * all the fragments.
     */
    *len = s->init_num = frag_len;
    return 1;

 f_err:
    s->init_num = 0;
    *len = 0;
    return 0;
}

/*-
 * for these 2 messages, we need to
 * ssl->enc_read_ctx                    re-init
 * ssl->rlayer.read_sequence            zero
 * ssl->s3->read_mac_secret             re-init
 * ssl->session->read_sym_enc           assign
 * ssl->session->read_compression       assign
 * ssl->session->read_hash              assign
 */
int dtls_construct_change_cipher_spec(SSL *s, WPACKET *pkt)
{
    if (s->version == DTLS1_BAD_VER) {
        s->d1->next_handshake_write_seq++;

        if (!WPACKET_put_bytes_u16(pkt, s->d1->handshake_write_seq)) {
            SSLfatal(s, SSL_AD_INTERNAL_ERROR,
                     SSL_F_DTLS_CONSTRUCT_CHANGE_CIPHER_SPEC,
                     ERR_R_INTERNAL_ERROR);
            return 0;
        }
    }

    return 1;
}

#ifndef OPENSSL_NO_SCTP
/*
 * Wait for a dry event. Should only be called at a point in the handshake
 * where we are not expecting any data from the peer except an alert.
 */
WORK_STATE dtls_wait_for_dry(SSL *s)
{
    int ret, errtype;
    size_t len;

    /* read app data until dry event */
    ret = BIO_dgram_sctp_wait_for_dry(SSL_get_wbio(s));
    if (ret < 0) {
        SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DTLS_WAIT_FOR_DRY,
                 ERR_R_INTERNAL_ERROR);
        return WORK_ERROR;
    }

    if (ret == 0) {
        /*
         * We're not expecting any more messages from the peer at this point -
         * but we could get an alert. If an alert is waiting then we will never
         * return successfully. Therefore we attempt to read a message. This
         * should never succeed but will process any waiting alerts.
         */
        if (dtls_get_reassembled_message(s, &errtype, &len)) {
            /* The call succeeded! This should never happen */
            SSLfatal(s, SSL_AD_UNEXPECTED_MESSAGE, SSL_F_DTLS_WAIT_FOR_DRY,
                     SSL_R_UNEXPECTED_MESSAGE);
            return WORK_ERROR;
        }

        s->s3->in_read_app_data = 2;
        s->rwstate = SSL_READING;
        BIO_clear_retry_flags(SSL_get_rbio(s));
        BIO_set_retry_read(SSL_get_rbio(s));
        return WORK_MORE_A;
    }
    return WORK_FINISHED_CONTINUE;
}
#endif

int dtls1_read_failed(SSL *s, int code)
{
    if (code > 0) {
        SSLfatal(s, SSL_AD_INTERNAL_ERROR,
                 SSL_F_DTLS1_READ_FAILED, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    if (!dtls1_is_timer_expired(s) || ossl_statem_in_error(s)) {
        /*
         * not a timeout, none of our business, let higher layers handle
         * this.  in fact it's probably an error
         */
        return code;
    }
    /* done, no need to send a retransmit */
    if (!SSL_in_init(s))
    {
        BIO_set_flags(SSL_get_rbio(s), BIO_FLAGS_READ);
        return code;
    }

    return dtls1_handle_timeout(s);
}

int dtls1_get_queue_priority(unsigned short seq, int is_ccs)
{
    /*
     * The index of the retransmission queue actually is the message sequence
     * number, since the queue only contains messages of a single handshake.
     * However, the ChangeCipherSpec has no message sequence number and so
     * using only the sequence will result in the CCS and Finished having the
     * same index. To prevent this, the sequence number is multiplied by 2.
     * In case of a CCS 1 is subtracted. This does not only differ CSS and
     * Finished, it also maintains the order of the index (important for
     * priority queues) and fits in the unsigned short variable.
     */
    return seq * 2 - is_ccs;
}

int dtls1_retransmit_buffered_messages(SSL *s)
{
    pqueue *sent = s->d1->sent_messages;
    piterator iter;
    pitem *item;
    hm_fragment *frag;
    int found = 0;

    iter = pqueue_iterator(sent);

    for (item = pqueue_next(&iter); item != NULL; item = pqueue_next(&iter)) {
        frag = (hm_fragment *)item->data;
        if (dtls1_retransmit_message(s, (unsigned short)
                                     dtls1_get_queue_priority
                                     (frag->msg_header.seq,
                                      frag->msg_header.is_ccs), &found) <= 0)
            return -1;
    }

    return 1;
}

int dtls1_buffer_message(SSL *s, int is_ccs)
{
    pitem *item;
    hm_fragment *frag;
    unsigned char seq64be[8];

    /*
     * this function is called immediately after a message has been
     * serialized
     */
    if (!ossl_assert(s->init_off == 0))
        return 0;

    frag = dtls1_hm_fragment_new(s->init_num, 0);
    if (frag == NULL)
        return 0;

    memcpy(frag->fragment, s->init_buf->data, s->init_num);

    if (is_ccs) {
        /* For DTLS1_BAD_VER the header length is non-standard */
        if (!ossl_assert(s->d1->w_msg_hdr.msg_len +
                         ((s->version ==
                           DTLS1_BAD_VER) ? 3 : DTLS1_CCS_HEADER_LENGTH)
                         == (unsigned int)s->init_num))
            return 0;
    } else {
        if (!ossl_assert(s->d1->w_msg_hdr.msg_len +
                         DTLS1_HM_HEADER_LENGTH == (unsigned int)s->init_num))
            return 0;
    }

    frag->msg_header.msg_len = s->d1->w_msg_hdr.msg_len;
    frag->msg_header.seq = s->d1->w_msg_hdr.seq;
    frag->msg_header.type = s->d1->w_msg_hdr.type;
    frag->msg_header.frag_off = 0;
    frag->msg_header.frag_len = s->d1->w_msg_hdr.msg_len;
    frag->msg_header.is_ccs = is_ccs;

    /* save current state */
    frag->msg_header.saved_retransmit_state.enc_write_ctx = s->enc_write_ctx;
    frag->msg_header.saved_retransmit_state.write_hash = s->write_hash;
    frag->msg_header.saved_retransmit_state.compress = s->compress;
    frag->msg_header.saved_retransmit_state.session = s->session;
    frag->msg_header.saved_retransmit_state.epoch =
        DTLS_RECORD_LAYER_get_w_epoch(&s->rlayer);

    memset(seq64be, 0, sizeof(seq64be));
    seq64be[6] =
        (unsigned
         char)(dtls1_get_queue_priority(frag->msg_header.seq,
                                        frag->msg_header.is_ccs) >> 8);
    seq64be[7] =
        (unsigned
         char)(dtls1_get_queue_priority(frag->msg_header.seq,
                                        frag->msg_header.is_ccs));

    item = pitem_new(seq64be, frag);
    if (item == NULL) {
        dtls1_hm_fragment_free(frag);
        return 0;
    }

    pqueue_insert(s->d1->sent_messages, item);
    return 1;
}

int dtls1_retransmit_message(SSL *s, unsigned short seq, int *found)
{
    int ret;
    /* XDTLS: for now assuming that read/writes are blocking */
    pitem *item;
    hm_fragment *frag;
    unsigned long header_length;
    unsigned char seq64be[8];
    struct dtls1_retransmit_state saved_state;

    /* XDTLS:  the requested message ought to be found, otherwise error */
    memset(seq64be, 0, sizeof(seq64be));
    seq64be[6] = (unsigned char)(seq >> 8);
    seq64be[7] = (unsigned char)seq;

    item = pqueue_find(s->d1->sent_messages, seq64be);
    if (item == NULL) {
        SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_DTLS1_RETRANSMIT_MESSAGE,
                 ERR_R_INTERNAL_ERROR);
        *found = 0;
        return 0;
    }

    *found = 1;
    frag = (hm_fragment *)item->data;

    if (frag->msg_header.is_ccs)
        header_length = DTLS1_CCS_HEADER_LENGTH;
    else
        header_length = DTLS1_HM_HEADER_LENGTH;

    memcpy(s->init_buf->data, frag->fragment,
           frag->msg_header.msg_len + header_length);
    s->init_num = frag->msg_header.msg_len + header_length;

    dtls1_set_message_header_int(s, frag->msg_header.type,
                                 frag->msg_header.msg_len,
                                 frag->msg_header.seq, 0,
                                 frag->msg_header.frag_len);

    /* save current state */
    saved_state.enc_write_ctx = s->enc_write_ctx;
    saved_state.write_hash = s->write_hash;
    saved_state.compress = s->compress;
    saved_state.session = s->session;
    saved_state.epoch = DTLS_RECORD_LAYER_get_w_epoch(&s->rlayer);

    s->d1->retransmitting = 1;

    /* restore state in which the message was originally sent */
    s->enc_write_ctx = frag->msg_header.saved_retransmit_state.enc_write_ctx;
    s->write_hash = frag->msg_header.saved_retransmit_state.write_hash;
    s->compress = frag->msg_header.saved_retransmit_state.compress;
    s->session = frag->msg_header.saved_retransmit_state.session;
    DTLS_RECORD_LAYER_set_saved_w_epoch(&s->rlayer,
                                        frag->msg_header.
                                        saved_retransmit_state.epoch);

    ret = dtls1_do_write(s, frag->msg_header.is_ccs ?
                         SSL3_RT_CHANGE_CIPHER_SPEC : SSL3_RT_HANDSHAKE);

    /* restore current state */
    s->enc_write_ctx = saved_state.enc_write_ctx;
    s->write_hash = saved_state.write_hash;
    s->compress = saved_state.compress;
    s->session = saved_state.session;
    DTLS_RECORD_LAYER_set_saved_w_epoch(&s->rlayer, saved_state.epoch);

    s->d1->retransmitting = 0;

    (void)BIO_flush(s->wbio);
    return ret;
}

void dtls1_set_message_header(SSL *s,
                              unsigned char mt, size_t len,
                              size_t frag_off, size_t frag_len)
{
    if (frag_off == 0) {
        s->d1->handshake_write_seq = s->d1->next_handshake_write_seq;
        s->d1->next_handshake_write_seq++;
    }

    dtls1_set_message_header_int(s, mt, len, s->d1->handshake_write_seq,
                                 frag_off, frag_len);
}

/* don't actually do the writing, wait till the MTU has been retrieved */
static void
dtls1_set_message_header_int(SSL *s, unsigned char mt,
                             size_t len, unsigned short seq_num,
                             size_t frag_off, size_t frag_len)
{
    struct hm_header_st *msg_hdr = &s->d1->w_msg_hdr;

    msg_hdr->type = mt;
    msg_hdr->msg_len = len;
    msg_hdr->seq = seq_num;
    msg_hdr->frag_off = frag_off;
    msg_hdr->frag_len = frag_len;
}

static void
dtls1_fix_message_header(SSL *s, size_t frag_off, size_t frag_len)
{
    struct hm_header_st *msg_hdr = &s->d1->w_msg_hdr;

    msg_hdr->frag_off = frag_off;
    msg_hdr->frag_len = frag_len;
}

static unsigned char *dtls1_write_message_header(SSL *s, unsigned char *p)
{
    struct hm_header_st *msg_hdr = &s->d1->w_msg_hdr;

    *p++ = msg_hdr->type;
    l2n3(msg_hdr->msg_len, p);

    s2n(msg_hdr->seq, p);
    l2n3(msg_hdr->frag_off, p);
    l2n3(msg_hdr->frag_len, p);

    return p;
}

void dtls1_get_message_header(unsigned char *data, struct hm_header_st *msg_hdr)
{
    memset(msg_hdr, 0, sizeof(*msg_hdr));
    msg_hdr->type = *(data++);
    n2l3(data, msg_hdr->msg_len);

    n2s(data, msg_hdr->seq);
    n2l3(data, msg_hdr->frag_off);
    n2l3(data, msg_hdr->frag_len);
}

int dtls1_set_handshake_header(SSL *s, WPACKET *pkt, int htype)
{
    unsigned char *header;

    if (htype == SSL3_MT_CHANGE_CIPHER_SPEC) {
        s->d1->handshake_write_seq = s->d1->next_handshake_write_seq;
        dtls1_set_message_header_int(s, SSL3_MT_CCS, 0,
                                     s->d1->handshake_write_seq, 0, 0);
        if (!WPACKET_put_bytes_u8(pkt, SSL3_MT_CCS))
            return 0;
    } else {
        dtls1_set_message_header(s, htype, 0, 0, 0);
        /*
         * We allocate space at the start for the message header. This gets
         * filled in later
         */
        if (!WPACKET_allocate_bytes(pkt, DTLS1_HM_HEADER_LENGTH, &header)
                || !WPACKET_start_sub_packet(pkt))
            return 0;
    }

    return 1;
}

int dtls1_close_construct_packet(SSL *s, WPACKET *pkt, int htype)
{
    size_t msglen;

    if ((htype != SSL3_MT_CHANGE_CIPHER_SPEC && !WPACKET_close(pkt))
            || !WPACKET_get_length(pkt, &msglen)
            || msglen > INT_MAX)
        return 0;

    if (htype != SSL3_MT_CHANGE_CIPHER_SPEC) {
        s->d1->w_msg_hdr.msg_len = msglen - DTLS1_HM_HEADER_LENGTH;
        s->d1->w_msg_hdr.frag_len = msglen - DTLS1_HM_HEADER_LENGTH;
    }
    s->init_num = (int)msglen;
    s->init_off = 0;

    if (htype != DTLS1_MT_HELLO_VERIFY_REQUEST) {
        /* Buffer the message to handle re-xmits */
        if (!dtls1_buffer_message(s, htype == SSL3_MT_CHANGE_CIPHER_SPEC
                                     ? 1 : 0))
            return 0;
    }

    return 1;
}
