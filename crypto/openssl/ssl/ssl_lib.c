/*
 * Copyright 1995-2019 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright (c) 2002, Oracle and/or its affiliates. All rights reserved
 * Copyright 2005 Nokia. All rights reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "ssl_locl.h"
#include <openssl/objects.h>
#include <openssl/x509v3.h>
#include <openssl/rand.h>
#include <openssl/rand_drbg.h>
#include <openssl/ocsp.h>
#include <openssl/dh.h>
#include <openssl/engine.h>
#include <openssl/async.h>
#include <openssl/ct.h>
#include "internal/cryptlib.h"
#include "internal/refcount.h"

const char SSL_version_str[] = OPENSSL_VERSION_TEXT;

static int ssl_undefined_function_1(SSL *ssl, SSL3_RECORD *r, size_t s, int t)
{
    (void)r;
    (void)s;
    (void)t;
    return ssl_undefined_function(ssl);
}

static int ssl_undefined_function_2(SSL *ssl, SSL3_RECORD *r, unsigned char *s,
                                    int t)
{
    (void)r;
    (void)s;
    (void)t;
    return ssl_undefined_function(ssl);
}

static int ssl_undefined_function_3(SSL *ssl, unsigned char *r,
                                    unsigned char *s, size_t t, size_t *u)
{
    (void)r;
    (void)s;
    (void)t;
    (void)u;
    return ssl_undefined_function(ssl);
}

static int ssl_undefined_function_4(SSL *ssl, int r)
{
    (void)r;
    return ssl_undefined_function(ssl);
}

static size_t ssl_undefined_function_5(SSL *ssl, const char *r, size_t s,
                                       unsigned char *t)
{
    (void)r;
    (void)s;
    (void)t;
    return ssl_undefined_function(ssl);
}

static int ssl_undefined_function_6(int r)
{
    (void)r;
    return ssl_undefined_function(NULL);
}

static int ssl_undefined_function_7(SSL *ssl, unsigned char *r, size_t s,
                                    const char *t, size_t u,
                                    const unsigned char *v, size_t w, int x)
{
    (void)r;
    (void)s;
    (void)t;
    (void)u;
    (void)v;
    (void)w;
    (void)x;
    return ssl_undefined_function(ssl);
}

SSL3_ENC_METHOD ssl3_undef_enc_method = {
    ssl_undefined_function_1,
    ssl_undefined_function_2,
    ssl_undefined_function,
    ssl_undefined_function_3,
    ssl_undefined_function_4,
    ssl_undefined_function_5,
    NULL,                       /* client_finished_label */
    0,                          /* client_finished_label_len */
    NULL,                       /* server_finished_label */
    0,                          /* server_finished_label_len */
    ssl_undefined_function_6,
    ssl_undefined_function_7,
};

struct ssl_async_args {
    SSL *s;
    void *buf;
    size_t num;
    enum { READFUNC, WRITEFUNC, OTHERFUNC } type;
    union {
        int (*func_read) (SSL *, void *, size_t, size_t *);
        int (*func_write) (SSL *, const void *, size_t, size_t *);
        int (*func_other) (SSL *);
    } f;
};

static const struct {
    uint8_t mtype;
    uint8_t ord;
    int nid;
} dane_mds[] = {
    {
        DANETLS_MATCHING_FULL, 0, NID_undef
    },
    {
        DANETLS_MATCHING_2256, 1, NID_sha256
    },
    {
        DANETLS_MATCHING_2512, 2, NID_sha512
    },
};

static int dane_ctx_enable(struct dane_ctx_st *dctx)
{
    const EVP_MD **mdevp;
    uint8_t *mdord;
    uint8_t mdmax = DANETLS_MATCHING_LAST;
    int n = ((int)mdmax) + 1;   /* int to handle PrivMatch(255) */
    size_t i;

    if (dctx->mdevp != NULL)
        return 1;

    mdevp = OPENSSL_zalloc(n * sizeof(*mdevp));
    mdord = OPENSSL_zalloc(n * sizeof(*mdord));

    if (mdord == NULL || mdevp == NULL) {
        OPENSSL_free(mdord);
        OPENSSL_free(mdevp);
        SSLerr(SSL_F_DANE_CTX_ENABLE, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    /* Install default entries */
    for (i = 0; i < OSSL_NELEM(dane_mds); ++i) {
        const EVP_MD *md;

        if (dane_mds[i].nid == NID_undef ||
            (md = EVP_get_digestbynid(dane_mds[i].nid)) == NULL)
            continue;
        mdevp[dane_mds[i].mtype] = md;
        mdord[dane_mds[i].mtype] = dane_mds[i].ord;
    }

    dctx->mdevp = mdevp;
    dctx->mdord = mdord;
    dctx->mdmax = mdmax;

    return 1;
}

static void dane_ctx_final(struct dane_ctx_st *dctx)
{
    OPENSSL_free(dctx->mdevp);
    dctx->mdevp = NULL;

    OPENSSL_free(dctx->mdord);
    dctx->mdord = NULL;
    dctx->mdmax = 0;
}

static void tlsa_free(danetls_record *t)
{
    if (t == NULL)
        return;
    OPENSSL_free(t->data);
    EVP_PKEY_free(t->spki);
    OPENSSL_free(t);
}

static void dane_final(SSL_DANE *dane)
{
    sk_danetls_record_pop_free(dane->trecs, tlsa_free);
    dane->trecs = NULL;

    sk_X509_pop_free(dane->certs, X509_free);
    dane->certs = NULL;

    X509_free(dane->mcert);
    dane->mcert = NULL;
    dane->mtlsa = NULL;
    dane->mdpth = -1;
    dane->pdpth = -1;
}

/*
 * dane_copy - Copy dane configuration, sans verification state.
 */
static int ssl_dane_dup(SSL *to, SSL *from)
{
    int num;
    int i;

    if (!DANETLS_ENABLED(&from->dane))
        return 1;

    num = sk_danetls_record_num(from->dane.trecs);
    dane_final(&to->dane);
    to->dane.flags = from->dane.flags;
    to->dane.dctx = &to->ctx->dane;
    to->dane.trecs = sk_danetls_record_new_reserve(NULL, num);

    if (to->dane.trecs == NULL) {
        SSLerr(SSL_F_SSL_DANE_DUP, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    for (i = 0; i < num; ++i) {
        danetls_record *t = sk_danetls_record_value(from->dane.trecs, i);

        if (SSL_dane_tlsa_add(to, t->usage, t->selector, t->mtype,
                              t->data, t->dlen) <= 0)
            return 0;
    }
    return 1;
}

static int dane_mtype_set(struct dane_ctx_st *dctx,
                          const EVP_MD *md, uint8_t mtype, uint8_t ord)
{
    int i;

    if (mtype == DANETLS_MATCHING_FULL && md != NULL) {
        SSLerr(SSL_F_DANE_MTYPE_SET, SSL_R_DANE_CANNOT_OVERRIDE_MTYPE_FULL);
        return 0;
    }

    if (mtype > dctx->mdmax) {
        const EVP_MD **mdevp;
        uint8_t *mdord;
        int n = ((int)mtype) + 1;

        mdevp = OPENSSL_realloc(dctx->mdevp, n * sizeof(*mdevp));
        if (mdevp == NULL) {
            SSLerr(SSL_F_DANE_MTYPE_SET, ERR_R_MALLOC_FAILURE);
            return -1;
        }
        dctx->mdevp = mdevp;

        mdord = OPENSSL_realloc(dctx->mdord, n * sizeof(*mdord));
        if (mdord == NULL) {
            SSLerr(SSL_F_DANE_MTYPE_SET, ERR_R_MALLOC_FAILURE);
            return -1;
        }
        dctx->mdord = mdord;

        /* Zero-fill any gaps */
        for (i = dctx->mdmax + 1; i < mtype; ++i) {
            mdevp[i] = NULL;
            mdord[i] = 0;
        }

        dctx->mdmax = mtype;
    }

    dctx->mdevp[mtype] = md;
    /* Coerce ordinal of disabled matching types to 0 */
    dctx->mdord[mtype] = (md == NULL) ? 0 : ord;

    return 1;
}

static const EVP_MD *tlsa_md_get(SSL_DANE *dane, uint8_t mtype)
{
    if (mtype > dane->dctx->mdmax)
        return NULL;
    return dane->dctx->mdevp[mtype];
}

static int dane_tlsa_add(SSL_DANE *dane,
                         uint8_t usage,
                         uint8_t selector,
                         uint8_t mtype, unsigned const char *data, size_t dlen)
{
    danetls_record *t;
    const EVP_MD *md = NULL;
    int ilen = (int)dlen;
    int i;
    int num;

    if (dane->trecs == NULL) {
        SSLerr(SSL_F_DANE_TLSA_ADD, SSL_R_DANE_NOT_ENABLED);
        return -1;
    }

    if (ilen < 0 || dlen != (size_t)ilen) {
        SSLerr(SSL_F_DANE_TLSA_ADD, SSL_R_DANE_TLSA_BAD_DATA_LENGTH);
        return 0;
    }

    if (usage > DANETLS_USAGE_LAST) {
        SSLerr(SSL_F_DANE_TLSA_ADD, SSL_R_DANE_TLSA_BAD_CERTIFICATE_USAGE);
        return 0;
    }

    if (selector > DANETLS_SELECTOR_LAST) {
        SSLerr(SSL_F_DANE_TLSA_ADD, SSL_R_DANE_TLSA_BAD_SELECTOR);
        return 0;
    }

    if (mtype != DANETLS_MATCHING_FULL) {
        md = tlsa_md_get(dane, mtype);
        if (md == NULL) {
            SSLerr(SSL_F_DANE_TLSA_ADD, SSL_R_DANE_TLSA_BAD_MATCHING_TYPE);
            return 0;
        }
    }

    if (md != NULL && dlen != (size_t)EVP_MD_size(md)) {
        SSLerr(SSL_F_DANE_TLSA_ADD, SSL_R_DANE_TLSA_BAD_DIGEST_LENGTH);
        return 0;
    }
    if (!data) {
        SSLerr(SSL_F_DANE_TLSA_ADD, SSL_R_DANE_TLSA_NULL_DATA);
        return 0;
    }

    if ((t = OPENSSL_zalloc(sizeof(*t))) == NULL) {
        SSLerr(SSL_F_DANE_TLSA_ADD, ERR_R_MALLOC_FAILURE);
        return -1;
    }

    t->usage = usage;
    t->selector = selector;
    t->mtype = mtype;
    t->data = OPENSSL_malloc(dlen);
    if (t->data == NULL) {
        tlsa_free(t);
        SSLerr(SSL_F_DANE_TLSA_ADD, ERR_R_MALLOC_FAILURE);
        return -1;
    }
    memcpy(t->data, data, dlen);
    t->dlen = dlen;

    /* Validate and cache full certificate or public key */
    if (mtype == DANETLS_MATCHING_FULL) {
        const unsigned char *p = data;
        X509 *cert = NULL;
        EVP_PKEY *pkey = NULL;

        switch (selector) {
        case DANETLS_SELECTOR_CERT:
            if (!d2i_X509(&cert, &p, ilen) || p < data ||
                dlen != (size_t)(p - data)) {
                tlsa_free(t);
                SSLerr(SSL_F_DANE_TLSA_ADD, SSL_R_DANE_TLSA_BAD_CERTIFICATE);
                return 0;
            }
            if (X509_get0_pubkey(cert) == NULL) {
                tlsa_free(t);
                SSLerr(SSL_F_DANE_TLSA_ADD, SSL_R_DANE_TLSA_BAD_CERTIFICATE);
                return 0;
            }

            if ((DANETLS_USAGE_BIT(usage) & DANETLS_TA_MASK) == 0) {
                X509_free(cert);
                break;
            }

            /*
             * For usage DANE-TA(2), we support authentication via "2 0 0" TLSA
             * records that contain full certificates of trust-anchors that are
             * not present in the wire chain.  For usage PKIX-TA(0), we augment
             * the chain with untrusted Full(0) certificates from DNS, in case
             * they are missing from the chain.
             */
            if ((dane->certs == NULL &&
                 (dane->certs = sk_X509_new_null()) == NULL) ||
                !sk_X509_push(dane->certs, cert)) {
                SSLerr(SSL_F_DANE_TLSA_ADD, ERR_R_MALLOC_FAILURE);
                X509_free(cert);
                tlsa_free(t);
                return -1;
            }
            break;

        case DANETLS_SELECTOR_SPKI:
            if (!d2i_PUBKEY(&pkey, &p, ilen) || p < data ||
                dlen != (size_t)(p - data)) {
                tlsa_free(t);
                SSLerr(SSL_F_DANE_TLSA_ADD, SSL_R_DANE_TLSA_BAD_PUBLIC_KEY);
                return 0;
            }

            /*
             * For usage DANE-TA(2), we support authentication via "2 1 0" TLSA
             * records that contain full bare keys of trust-anchors that are
             * not present in the wire chain.
             */
            if (usage == DANETLS_USAGE_DANE_TA)
                t->spki = pkey;
            else
                EVP_PKEY_free(pkey);
            break;
        }
    }

    /*-
     * Find the right insertion point for the new record.
     *
     * See crypto/x509/x509_vfy.c.  We sort DANE-EE(3) records first, so that
     * they can be processed first, as they require no chain building, and no
     * expiration or hostname checks.  Because DANE-EE(3) is numerically
     * largest, this is accomplished via descending sort by "usage".
     *
     * We also sort in descending order by matching ordinal to simplify
     * the implementation of digest agility in the verification code.
     *
     * The choice of order for the selector is not significant, so we
     * use the same descending order for consistency.
     */
    num = sk_danetls_record_num(dane->trecs);
    for (i = 0; i < num; ++i) {
        danetls_record *rec = sk_danetls_record_value(dane->trecs, i);

        if (rec->usage > usage)
            continue;
        if (rec->usage < usage)
            break;
        if (rec->selector > selector)
            continue;
        if (rec->selector < selector)
            break;
        if (dane->dctx->mdord[rec->mtype] > dane->dctx->mdord[mtype])
            continue;
        break;
    }

    if (!sk_danetls_record_insert(dane->trecs, t, i)) {
        tlsa_free(t);
        SSLerr(SSL_F_DANE_TLSA_ADD, ERR_R_MALLOC_FAILURE);
        return -1;
    }
    dane->umask |= DANETLS_USAGE_BIT(usage);

    return 1;
}

/*
 * Return 0 if there is only one version configured and it was disabled
 * at configure time.  Return 1 otherwise.
 */
static int ssl_check_allowed_versions(int min_version, int max_version)
{
    int minisdtls = 0, maxisdtls = 0;

    /* Figure out if we're doing DTLS versions or TLS versions */
    if (min_version == DTLS1_BAD_VER
        || min_version >> 8 == DTLS1_VERSION_MAJOR)
        minisdtls = 1;
    if (max_version == DTLS1_BAD_VER
        || max_version >> 8 == DTLS1_VERSION_MAJOR)
        maxisdtls = 1;
    /* A wildcard version of 0 could be DTLS or TLS. */
    if ((minisdtls && !maxisdtls && max_version != 0)
        || (maxisdtls && !minisdtls && min_version != 0)) {
        /* Mixing DTLS and TLS versions will lead to sadness; deny it. */
        return 0;
    }

    if (minisdtls || maxisdtls) {
        /* Do DTLS version checks. */
        if (min_version == 0)
            /* Ignore DTLS1_BAD_VER */
            min_version = DTLS1_VERSION;
        if (max_version == 0)
            max_version = DTLS1_2_VERSION;
#ifdef OPENSSL_NO_DTLS1_2
        if (max_version == DTLS1_2_VERSION)
            max_version = DTLS1_VERSION;
#endif
#ifdef OPENSSL_NO_DTLS1
        if (min_version == DTLS1_VERSION)
            min_version = DTLS1_2_VERSION;
#endif
        /* Done massaging versions; do the check. */
        if (0
#ifdef OPENSSL_NO_DTLS1
            || (DTLS_VERSION_GE(min_version, DTLS1_VERSION)
                && DTLS_VERSION_GE(DTLS1_VERSION, max_version))
#endif
#ifdef OPENSSL_NO_DTLS1_2
            || (DTLS_VERSION_GE(min_version, DTLS1_2_VERSION)
                && DTLS_VERSION_GE(DTLS1_2_VERSION, max_version))
#endif
            )
            return 0;
    } else {
        /* Regular TLS version checks. */
        if (min_version == 0)
            min_version = SSL3_VERSION;
        if (max_version == 0)
            max_version = TLS1_3_VERSION;
#ifdef OPENSSL_NO_TLS1_3
        if (max_version == TLS1_3_VERSION)
            max_version = TLS1_2_VERSION;
#endif
#ifdef OPENSSL_NO_TLS1_2
        if (max_version == TLS1_2_VERSION)
            max_version = TLS1_1_VERSION;
#endif
#ifdef OPENSSL_NO_TLS1_1
        if (max_version == TLS1_1_VERSION)
            max_version = TLS1_VERSION;
#endif
#ifdef OPENSSL_NO_TLS1
        if (max_version == TLS1_VERSION)
            max_version = SSL3_VERSION;
#endif
#ifdef OPENSSL_NO_SSL3
        if (min_version == SSL3_VERSION)
            min_version = TLS1_VERSION;
#endif
#ifdef OPENSSL_NO_TLS1
        if (min_version == TLS1_VERSION)
            min_version = TLS1_1_VERSION;
#endif
#ifdef OPENSSL_NO_TLS1_1
        if (min_version == TLS1_1_VERSION)
            min_version = TLS1_2_VERSION;
#endif
#ifdef OPENSSL_NO_TLS1_2
        if (min_version == TLS1_2_VERSION)
            min_version = TLS1_3_VERSION;
#endif
        /* Done massaging versions; do the check. */
        if (0
#ifdef OPENSSL_NO_SSL3
            || (min_version <= SSL3_VERSION && SSL3_VERSION <= max_version)
#endif
#ifdef OPENSSL_NO_TLS1
            || (min_version <= TLS1_VERSION && TLS1_VERSION <= max_version)
#endif
#ifdef OPENSSL_NO_TLS1_1
            || (min_version <= TLS1_1_VERSION && TLS1_1_VERSION <= max_version)
#endif
#ifdef OPENSSL_NO_TLS1_2
            || (min_version <= TLS1_2_VERSION && TLS1_2_VERSION <= max_version)
#endif
#ifdef OPENSSL_NO_TLS1_3
            || (min_version <= TLS1_3_VERSION && TLS1_3_VERSION <= max_version)
#endif
            )
            return 0;
    }
    return 1;
}

static void clear_ciphers(SSL *s)
{
    /* clear the current cipher */
    ssl_clear_cipher_ctx(s);
    ssl_clear_hash_ctx(&s->read_hash);
    ssl_clear_hash_ctx(&s->write_hash);
}

int SSL_clear(SSL *s)
{
    if (s->method == NULL) {
        SSLerr(SSL_F_SSL_CLEAR, SSL_R_NO_METHOD_SPECIFIED);
        return 0;
    }

    if (ssl_clear_bad_session(s)) {
        SSL_SESSION_free(s->session);
        s->session = NULL;
    }
    SSL_SESSION_free(s->psksession);
    s->psksession = NULL;
    OPENSSL_free(s->psksession_id);
    s->psksession_id = NULL;
    s->psksession_id_len = 0;
    s->hello_retry_request = 0;
    s->sent_tickets = 0;

    s->error = 0;
    s->hit = 0;
    s->shutdown = 0;

    if (s->renegotiate) {
        SSLerr(SSL_F_SSL_CLEAR, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    ossl_statem_clear(s);

    s->version = s->method->version;
    s->client_version = s->version;
    s->rwstate = SSL_NOTHING;

    BUF_MEM_free(s->init_buf);
    s->init_buf = NULL;
    clear_ciphers(s);
    s->first_packet = 0;

    s->key_update = SSL_KEY_UPDATE_NONE;

    EVP_MD_CTX_free(s->pha_dgst);
    s->pha_dgst = NULL;

    /* Reset DANE verification result state */
    s->dane.mdpth = -1;
    s->dane.pdpth = -1;
    X509_free(s->dane.mcert);
    s->dane.mcert = NULL;
    s->dane.mtlsa = NULL;

    /* Clear the verification result peername */
    X509_VERIFY_PARAM_move_peername(s->param, NULL);

    /*
     * Check to see if we were changed into a different method, if so, revert
     * back.
     */
    if (s->method != s->ctx->method) {
        s->method->ssl_free(s);
        s->method = s->ctx->method;
        if (!s->method->ssl_new(s))
            return 0;
    } else {
        if (!s->method->ssl_clear(s))
            return 0;
    }

    RECORD_LAYER_clear(&s->rlayer);

    return 1;
}

/** Used to change an SSL_CTXs default SSL method type */
int SSL_CTX_set_ssl_version(SSL_CTX *ctx, const SSL_METHOD *meth)
{
    STACK_OF(SSL_CIPHER) *sk;

    ctx->method = meth;

    if (!SSL_CTX_set_ciphersuites(ctx, TLS_DEFAULT_CIPHERSUITES)) {
        SSLerr(SSL_F_SSL_CTX_SET_SSL_VERSION, SSL_R_SSL_LIBRARY_HAS_NO_CIPHERS);
        return 0;
    }
    sk = ssl_create_cipher_list(ctx->method,
                                ctx->tls13_ciphersuites,
                                &(ctx->cipher_list),
                                &(ctx->cipher_list_by_id),
                                SSL_DEFAULT_CIPHER_LIST, ctx->cert);
    if ((sk == NULL) || (sk_SSL_CIPHER_num(sk) <= 0)) {
        SSLerr(SSL_F_SSL_CTX_SET_SSL_VERSION, SSL_R_SSL_LIBRARY_HAS_NO_CIPHERS);
        return 0;
    }
    return 1;
}

SSL *SSL_new(SSL_CTX *ctx)
{
    SSL *s;

    if (ctx == NULL) {
        SSLerr(SSL_F_SSL_NEW, SSL_R_NULL_SSL_CTX);
        return NULL;
    }
    if (ctx->method == NULL) {
        SSLerr(SSL_F_SSL_NEW, SSL_R_SSL_CTX_HAS_NO_DEFAULT_SSL_VERSION);
        return NULL;
    }

    s = OPENSSL_zalloc(sizeof(*s));
    if (s == NULL)
        goto err;

    s->references = 1;
    s->lock = CRYPTO_THREAD_lock_new();
    if (s->lock == NULL) {
        OPENSSL_free(s);
        s = NULL;
        goto err;
    }

    RECORD_LAYER_init(&s->rlayer, s);

    s->options = ctx->options;
    s->dane.flags = ctx->dane.flags;
    s->min_proto_version = ctx->min_proto_version;
    s->max_proto_version = ctx->max_proto_version;
    s->mode = ctx->mode;
    s->max_cert_list = ctx->max_cert_list;
    s->max_early_data = ctx->max_early_data;
    s->recv_max_early_data = ctx->recv_max_early_data;
    s->num_tickets = ctx->num_tickets;
    s->pha_enabled = ctx->pha_enabled;

    /* Shallow copy of the ciphersuites stack */
    s->tls13_ciphersuites = sk_SSL_CIPHER_dup(ctx->tls13_ciphersuites);
    if (s->tls13_ciphersuites == NULL)
        goto err;

    /*
     * Earlier library versions used to copy the pointer to the CERT, not
     * its contents; only when setting new parameters for the per-SSL
     * copy, ssl_cert_new would be called (and the direct reference to
     * the per-SSL_CTX settings would be lost, but those still were
     * indirectly accessed for various purposes, and for that reason they
     * used to be known as s->ctx->default_cert). Now we don't look at the
     * SSL_CTX's CERT after having duplicated it once.
     */
    s->cert = ssl_cert_dup(ctx->cert);
    if (s->cert == NULL)
        goto err;

    RECORD_LAYER_set_read_ahead(&s->rlayer, ctx->read_ahead);
    s->msg_callback = ctx->msg_callback;
    s->msg_callback_arg = ctx->msg_callback_arg;
    s->verify_mode = ctx->verify_mode;
    s->not_resumable_session_cb = ctx->not_resumable_session_cb;
    s->record_padding_cb = ctx->record_padding_cb;
    s->record_padding_arg = ctx->record_padding_arg;
    s->block_padding = ctx->block_padding;
    s->sid_ctx_length = ctx->sid_ctx_length;
    if (!ossl_assert(s->sid_ctx_length <= sizeof(s->sid_ctx)))
        goto err;
    memcpy(&s->sid_ctx, &ctx->sid_ctx, sizeof(s->sid_ctx));
    s->verify_callback = ctx->default_verify_callback;
    s->generate_session_id = ctx->generate_session_id;

    s->param = X509_VERIFY_PARAM_new();
    if (s->param == NULL)
        goto err;
    X509_VERIFY_PARAM_inherit(s->param, ctx->param);
    s->quiet_shutdown = ctx->quiet_shutdown;

    s->ext.max_fragment_len_mode = ctx->ext.max_fragment_len_mode;
    s->max_send_fragment = ctx->max_send_fragment;
    s->split_send_fragment = ctx->split_send_fragment;
    s->max_pipelines = ctx->max_pipelines;
    if (s->max_pipelines > 1)
        RECORD_LAYER_set_read_ahead(&s->rlayer, 1);
    if (ctx->default_read_buf_len > 0)
        SSL_set_default_read_buffer_len(s, ctx->default_read_buf_len);

    SSL_CTX_up_ref(ctx);
    s->ctx = ctx;
    s->ext.debug_cb = 0;
    s->ext.debug_arg = NULL;
    s->ext.ticket_expected = 0;
    s->ext.status_type = ctx->ext.status_type;
    s->ext.status_expected = 0;
    s->ext.ocsp.ids = NULL;
    s->ext.ocsp.exts = NULL;
    s->ext.ocsp.resp = NULL;
    s->ext.ocsp.resp_len = 0;
    SSL_CTX_up_ref(ctx);
    s->session_ctx = ctx;
#ifndef OPENSSL_NO_EC
    if (ctx->ext.ecpointformats) {
        s->ext.ecpointformats =
            OPENSSL_memdup(ctx->ext.ecpointformats,
                           ctx->ext.ecpointformats_len);
        if (!s->ext.ecpointformats)
            goto err;
        s->ext.ecpointformats_len =
            ctx->ext.ecpointformats_len;
    }
    if (ctx->ext.supportedgroups) {
        s->ext.supportedgroups =
            OPENSSL_memdup(ctx->ext.supportedgroups,
                           ctx->ext.supportedgroups_len
                                * sizeof(*ctx->ext.supportedgroups));
        if (!s->ext.supportedgroups)
            goto err;
        s->ext.supportedgroups_len = ctx->ext.supportedgroups_len;
    }
#endif
#ifndef OPENSSL_NO_NEXTPROTONEG
    s->ext.npn = NULL;
#endif

    if (s->ctx->ext.alpn) {
        s->ext.alpn = OPENSSL_malloc(s->ctx->ext.alpn_len);
        if (s->ext.alpn == NULL)
            goto err;
        memcpy(s->ext.alpn, s->ctx->ext.alpn, s->ctx->ext.alpn_len);
        s->ext.alpn_len = s->ctx->ext.alpn_len;
    }

    s->verified_chain = NULL;
    s->verify_result = X509_V_OK;

    s->default_passwd_callback = ctx->default_passwd_callback;
    s->default_passwd_callback_userdata = ctx->default_passwd_callback_userdata;

    s->method = ctx->method;

    s->key_update = SSL_KEY_UPDATE_NONE;

    s->allow_early_data_cb = ctx->allow_early_data_cb;
    s->allow_early_data_cb_data = ctx->allow_early_data_cb_data;

    if (!s->method->ssl_new(s))
        goto err;

    s->server = (ctx->method->ssl_accept == ssl_undefined_function) ? 0 : 1;

    if (!SSL_clear(s))
        goto err;

    if (!CRYPTO_new_ex_data(CRYPTO_EX_INDEX_SSL, s, &s->ex_data))
        goto err;

#ifndef OPENSSL_NO_PSK
    s->psk_client_callback = ctx->psk_client_callback;
    s->psk_server_callback = ctx->psk_server_callback;
#endif
    s->psk_find_session_cb = ctx->psk_find_session_cb;
    s->psk_use_session_cb = ctx->psk_use_session_cb;

    s->job = NULL;

#ifndef OPENSSL_NO_CT
    if (!SSL_set_ct_validation_callback(s, ctx->ct_validation_callback,
                                        ctx->ct_validation_callback_arg))
        goto err;
#endif

    return s;
 err:
    SSL_free(s);
    SSLerr(SSL_F_SSL_NEW, ERR_R_MALLOC_FAILURE);
    return NULL;
}

int SSL_is_dtls(const SSL *s)
{
    return SSL_IS_DTLS(s) ? 1 : 0;
}

int SSL_up_ref(SSL *s)
{
    int i;

    if (CRYPTO_UP_REF(&s->references, &i, s->lock) <= 0)
        return 0;

    REF_PRINT_COUNT("SSL", s);
    REF_ASSERT_ISNT(i < 2);
    return ((i > 1) ? 1 : 0);
}

int SSL_CTX_set_session_id_context(SSL_CTX *ctx, const unsigned char *sid_ctx,
                                   unsigned int sid_ctx_len)
{
    if (sid_ctx_len > sizeof(ctx->sid_ctx)) {
        SSLerr(SSL_F_SSL_CTX_SET_SESSION_ID_CONTEXT,
               SSL_R_SSL_SESSION_ID_CONTEXT_TOO_LONG);
        return 0;
    }
    ctx->sid_ctx_length = sid_ctx_len;
    memcpy(ctx->sid_ctx, sid_ctx, sid_ctx_len);

    return 1;
}

int SSL_set_session_id_context(SSL *ssl, const unsigned char *sid_ctx,
                               unsigned int sid_ctx_len)
{
    if (sid_ctx_len > SSL_MAX_SID_CTX_LENGTH) {
        SSLerr(SSL_F_SSL_SET_SESSION_ID_CONTEXT,
               SSL_R_SSL_SESSION_ID_CONTEXT_TOO_LONG);
        return 0;
    }
    ssl->sid_ctx_length = sid_ctx_len;
    memcpy(ssl->sid_ctx, sid_ctx, sid_ctx_len);

    return 1;
}

int SSL_CTX_set_generate_session_id(SSL_CTX *ctx, GEN_SESSION_CB cb)
{
    CRYPTO_THREAD_write_lock(ctx->lock);
    ctx->generate_session_id = cb;
    CRYPTO_THREAD_unlock(ctx->lock);
    return 1;
}

int SSL_set_generate_session_id(SSL *ssl, GEN_SESSION_CB cb)
{
    CRYPTO_THREAD_write_lock(ssl->lock);
    ssl->generate_session_id = cb;
    CRYPTO_THREAD_unlock(ssl->lock);
    return 1;
}

int SSL_has_matching_session_id(const SSL *ssl, const unsigned char *id,
                                unsigned int id_len)
{
    /*
     * A quick examination of SSL_SESSION_hash and SSL_SESSION_cmp shows how
     * we can "construct" a session to give us the desired check - i.e. to
     * find if there's a session in the hash table that would conflict with
     * any new session built out of this id/id_len and the ssl_version in use
     * by this SSL.
     */
    SSL_SESSION r, *p;

    if (id_len > sizeof(r.session_id))
        return 0;

    r.ssl_version = ssl->version;
    r.session_id_length = id_len;
    memcpy(r.session_id, id, id_len);

    CRYPTO_THREAD_read_lock(ssl->session_ctx->lock);
    p = lh_SSL_SESSION_retrieve(ssl->session_ctx->sessions, &r);
    CRYPTO_THREAD_unlock(ssl->session_ctx->lock);
    return (p != NULL);
}

int SSL_CTX_set_purpose(SSL_CTX *s, int purpose)
{
    return X509_VERIFY_PARAM_set_purpose(s->param, purpose);
}

int SSL_set_purpose(SSL *s, int purpose)
{
    return X509_VERIFY_PARAM_set_purpose(s->param, purpose);
}

int SSL_CTX_set_trust(SSL_CTX *s, int trust)
{
    return X509_VERIFY_PARAM_set_trust(s->param, trust);
}

int SSL_set_trust(SSL *s, int trust)
{
    return X509_VERIFY_PARAM_set_trust(s->param, trust);
}

int SSL_set1_host(SSL *s, const char *hostname)
{
    return X509_VERIFY_PARAM_set1_host(s->param, hostname, 0);
}

int SSL_add1_host(SSL *s, const char *hostname)
{
    return X509_VERIFY_PARAM_add1_host(s->param, hostname, 0);
}

void SSL_set_hostflags(SSL *s, unsigned int flags)
{
    X509_VERIFY_PARAM_set_hostflags(s->param, flags);
}

const char *SSL_get0_peername(SSL *s)
{
    return X509_VERIFY_PARAM_get0_peername(s->param);
}

int SSL_CTX_dane_enable(SSL_CTX *ctx)
{
    return dane_ctx_enable(&ctx->dane);
}

unsigned long SSL_CTX_dane_set_flags(SSL_CTX *ctx, unsigned long flags)
{
    unsigned long orig = ctx->dane.flags;

    ctx->dane.flags |= flags;
    return orig;
}

unsigned long SSL_CTX_dane_clear_flags(SSL_CTX *ctx, unsigned long flags)
{
    unsigned long orig = ctx->dane.flags;

    ctx->dane.flags &= ~flags;
    return orig;
}

int SSL_dane_enable(SSL *s, const char *basedomain)
{
    SSL_DANE *dane = &s->dane;

    if (s->ctx->dane.mdmax == 0) {
        SSLerr(SSL_F_SSL_DANE_ENABLE, SSL_R_CONTEXT_NOT_DANE_ENABLED);
        return 0;
    }
    if (dane->trecs != NULL) {
        SSLerr(SSL_F_SSL_DANE_ENABLE, SSL_R_DANE_ALREADY_ENABLED);
        return 0;
    }

    /*
     * Default SNI name.  This rejects empty names, while set1_host below
     * accepts them and disables host name checks.  To avoid side-effects with
     * invalid input, set the SNI name first.
     */
    if (s->ext.hostname == NULL) {
        if (!SSL_set_tlsext_host_name(s, basedomain)) {
            SSLerr(SSL_F_SSL_DANE_ENABLE, SSL_R_ERROR_SETTING_TLSA_BASE_DOMAIN);
            return -1;
        }
    }

    /* Primary RFC6125 reference identifier */
    if (!X509_VERIFY_PARAM_set1_host(s->param, basedomain, 0)) {
        SSLerr(SSL_F_SSL_DANE_ENABLE, SSL_R_ERROR_SETTING_TLSA_BASE_DOMAIN);
        return -1;
    }

    dane->mdpth = -1;
    dane->pdpth = -1;
    dane->dctx = &s->ctx->dane;
    dane->trecs = sk_danetls_record_new_null();

    if (dane->trecs == NULL) {
        SSLerr(SSL_F_SSL_DANE_ENABLE, ERR_R_MALLOC_FAILURE);
        return -1;
    }
    return 1;
}

unsigned long SSL_dane_set_flags(SSL *ssl, unsigned long flags)
{
    unsigned long orig = ssl->dane.flags;

    ssl->dane.flags |= flags;
    return orig;
}

unsigned long SSL_dane_clear_flags(SSL *ssl, unsigned long flags)
{
    unsigned long orig = ssl->dane.flags;

    ssl->dane.flags &= ~flags;
    return orig;
}

int SSL_get0_dane_authority(SSL *s, X509 **mcert, EVP_PKEY **mspki)
{
    SSL_DANE *dane = &s->dane;

    if (!DANETLS_ENABLED(dane) || s->verify_result != X509_V_OK)
        return -1;
    if (dane->mtlsa) {
        if (mcert)
            *mcert = dane->mcert;
        if (mspki)
            *mspki = (dane->mcert == NULL) ? dane->mtlsa->spki : NULL;
    }
    return dane->mdpth;
}

int SSL_get0_dane_tlsa(SSL *s, uint8_t *usage, uint8_t *selector,
                       uint8_t *mtype, unsigned const char **data, size_t *dlen)
{
    SSL_DANE *dane = &s->dane;

    if (!DANETLS_ENABLED(dane) || s->verify_result != X509_V_OK)
        return -1;
    if (dane->mtlsa) {
        if (usage)
            *usage = dane->mtlsa->usage;
        if (selector)
            *selector = dane->mtlsa->selector;
        if (mtype)
            *mtype = dane->mtlsa->mtype;
        if (data)
            *data = dane->mtlsa->data;
        if (dlen)
            *dlen = dane->mtlsa->dlen;
    }
    return dane->mdpth;
}

SSL_DANE *SSL_get0_dane(SSL *s)
{
    return &s->dane;
}

int SSL_dane_tlsa_add(SSL *s, uint8_t usage, uint8_t selector,
                      uint8_t mtype, unsigned const char *data, size_t dlen)
{
    return dane_tlsa_add(&s->dane, usage, selector, mtype, data, dlen);
}

int SSL_CTX_dane_mtype_set(SSL_CTX *ctx, const EVP_MD *md, uint8_t mtype,
                           uint8_t ord)
{
    return dane_mtype_set(&ctx->dane, md, mtype, ord);
}

int SSL_CTX_set1_param(SSL_CTX *ctx, X509_VERIFY_PARAM *vpm)
{
    return X509_VERIFY_PARAM_set1(ctx->param, vpm);
}

int SSL_set1_param(SSL *ssl, X509_VERIFY_PARAM *vpm)
{
    return X509_VERIFY_PARAM_set1(ssl->param, vpm);
}

X509_VERIFY_PARAM *SSL_CTX_get0_param(SSL_CTX *ctx)
{
    return ctx->param;
}

X509_VERIFY_PARAM *SSL_get0_param(SSL *ssl)
{
    return ssl->param;
}

void SSL_certs_clear(SSL *s)
{
    ssl_cert_clear_certs(s->cert);
}

void SSL_free(SSL *s)
{
    int i;

    if (s == NULL)
        return;
    CRYPTO_DOWN_REF(&s->references, &i, s->lock);
    REF_PRINT_COUNT("SSL", s);
    if (i > 0)
        return;
    REF_ASSERT_ISNT(i < 0);

    X509_VERIFY_PARAM_free(s->param);
    dane_final(&s->dane);
    CRYPTO_free_ex_data(CRYPTO_EX_INDEX_SSL, s, &s->ex_data);

    /* Ignore return value */
    ssl_free_wbio_buffer(s);

    BIO_free_all(s->wbio);
    BIO_free_all(s->rbio);

    BUF_MEM_free(s->init_buf);

    /* add extra stuff */
    sk_SSL_CIPHER_free(s->cipher_list);
    sk_SSL_CIPHER_free(s->cipher_list_by_id);
    sk_SSL_CIPHER_free(s->tls13_ciphersuites);

    /* Make the next call work :-) */
    if (s->session != NULL) {
        ssl_clear_bad_session(s);
        SSL_SESSION_free(s->session);
    }
    SSL_SESSION_free(s->psksession);
    OPENSSL_free(s->psksession_id);

    clear_ciphers(s);

    ssl_cert_free(s->cert);
    /* Free up if allocated */

    OPENSSL_free(s->ext.hostname);
    SSL_CTX_free(s->session_ctx);
#ifndef OPENSSL_NO_EC
    OPENSSL_free(s->ext.ecpointformats);
    OPENSSL_free(s->ext.supportedgroups);
#endif                          /* OPENSSL_NO_EC */
    sk_X509_EXTENSION_pop_free(s->ext.ocsp.exts, X509_EXTENSION_free);
#ifndef OPENSSL_NO_OCSP
    sk_OCSP_RESPID_pop_free(s->ext.ocsp.ids, OCSP_RESPID_free);
#endif
#ifndef OPENSSL_NO_CT
    SCT_LIST_free(s->scts);
    OPENSSL_free(s->ext.scts);
#endif
    OPENSSL_free(s->ext.ocsp.resp);
    OPENSSL_free(s->ext.alpn);
    OPENSSL_free(s->ext.tls13_cookie);
    OPENSSL_free(s->clienthello);
    OPENSSL_free(s->pha_context);
    EVP_MD_CTX_free(s->pha_dgst);

    sk_X509_NAME_pop_free(s->ca_names, X509_NAME_free);
    sk_X509_NAME_pop_free(s->client_ca_names, X509_NAME_free);

    sk_X509_pop_free(s->verified_chain, X509_free);

    if (s->method != NULL)
        s->method->ssl_free(s);

    RECORD_LAYER_release(&s->rlayer);

    SSL_CTX_free(s->ctx);

    ASYNC_WAIT_CTX_free(s->waitctx);

#if !defined(OPENSSL_NO_NEXTPROTONEG)
    OPENSSL_free(s->ext.npn);
#endif

#ifndef OPENSSL_NO_SRTP
    sk_SRTP_PROTECTION_PROFILE_free(s->srtp_profiles);
#endif

    CRYPTO_THREAD_lock_free(s->lock);

    OPENSSL_free(s);
}

void SSL_set0_rbio(SSL *s, BIO *rbio)
{
    BIO_free_all(s->rbio);
    s->rbio = rbio;
}

void SSL_set0_wbio(SSL *s, BIO *wbio)
{
    /*
     * If the output buffering BIO is still in place, remove it
     */
    if (s->bbio != NULL)
        s->wbio = BIO_pop(s->wbio);

    BIO_free_all(s->wbio);
    s->wbio = wbio;

    /* Re-attach |bbio| to the new |wbio|. */
    if (s->bbio != NULL)
        s->wbio = BIO_push(s->bbio, s->wbio);
}

void SSL_set_bio(SSL *s, BIO *rbio, BIO *wbio)
{
    /*
     * For historical reasons, this function has many different cases in
     * ownership handling.
     */

    /* If nothing has changed, do nothing */
    if (rbio == SSL_get_rbio(s) && wbio == SSL_get_wbio(s))
        return;

    /*
     * If the two arguments are equal then one fewer reference is granted by the
     * caller than we want to take
     */
    if (rbio != NULL && rbio == wbio)
        BIO_up_ref(rbio);

    /*
     * If only the wbio is changed only adopt one reference.
     */
    if (rbio == SSL_get_rbio(s)) {
        SSL_set0_wbio(s, wbio);
        return;
    }
    /*
     * There is an asymmetry here for historical reasons. If only the rbio is
     * changed AND the rbio and wbio were originally different, then we only
     * adopt one reference.
     */
    if (wbio == SSL_get_wbio(s) && SSL_get_rbio(s) != SSL_get_wbio(s)) {
        SSL_set0_rbio(s, rbio);
        return;
    }

    /* Otherwise, adopt both references. */
    SSL_set0_rbio(s, rbio);
    SSL_set0_wbio(s, wbio);
}

BIO *SSL_get_rbio(const SSL *s)
{
    return s->rbio;
}

BIO *SSL_get_wbio(const SSL *s)
{
    if (s->bbio != NULL) {
        /*
         * If |bbio| is active, the true caller-configured BIO is its
         * |next_bio|.
         */
        return BIO_next(s->bbio);
    }
    return s->wbio;
}

int SSL_get_fd(const SSL *s)
{
    return SSL_get_rfd(s);
}

int SSL_get_rfd(const SSL *s)
{
    int ret = -1;
    BIO *b, *r;

    b = SSL_get_rbio(s);
    r = BIO_find_type(b, BIO_TYPE_DESCRIPTOR);
    if (r != NULL)
        BIO_get_fd(r, &ret);
    return ret;
}

int SSL_get_wfd(const SSL *s)
{
    int ret = -1;
    BIO *b, *r;

    b = SSL_get_wbio(s);
    r = BIO_find_type(b, BIO_TYPE_DESCRIPTOR);
    if (r != NULL)
        BIO_get_fd(r, &ret);
    return ret;
}

#ifndef OPENSSL_NO_SOCK
int SSL_set_fd(SSL *s, int fd)
{
    int ret = 0;
    BIO *bio = NULL;

    bio = BIO_new(BIO_s_socket());

    if (bio == NULL) {
        SSLerr(SSL_F_SSL_SET_FD, ERR_R_BUF_LIB);
        goto err;
    }
    BIO_set_fd(bio, fd, BIO_NOCLOSE);
    SSL_set_bio(s, bio, bio);
    ret = 1;
 err:
    return ret;
}

int SSL_set_wfd(SSL *s, int fd)
{
    BIO *rbio = SSL_get_rbio(s);

    if (rbio == NULL || BIO_method_type(rbio) != BIO_TYPE_SOCKET
        || (int)BIO_get_fd(rbio, NULL) != fd) {
        BIO *bio = BIO_new(BIO_s_socket());

        if (bio == NULL) {
            SSLerr(SSL_F_SSL_SET_WFD, ERR_R_BUF_LIB);
            return 0;
        }
        BIO_set_fd(bio, fd, BIO_NOCLOSE);
        SSL_set0_wbio(s, bio);
    } else {
        BIO_up_ref(rbio);
        SSL_set0_wbio(s, rbio);
    }
    return 1;
}

int SSL_set_rfd(SSL *s, int fd)
{
    BIO *wbio = SSL_get_wbio(s);

    if (wbio == NULL || BIO_method_type(wbio) != BIO_TYPE_SOCKET
        || ((int)BIO_get_fd(wbio, NULL) != fd)) {
        BIO *bio = BIO_new(BIO_s_socket());

        if (bio == NULL) {
            SSLerr(SSL_F_SSL_SET_RFD, ERR_R_BUF_LIB);
            return 0;
        }
        BIO_set_fd(bio, fd, BIO_NOCLOSE);
        SSL_set0_rbio(s, bio);
    } else {
        BIO_up_ref(wbio);
        SSL_set0_rbio(s, wbio);
    }

    return 1;
}
#endif

/* return length of latest Finished message we sent, copy to 'buf' */
size_t SSL_get_finished(const SSL *s, void *buf, size_t count)
{
    size_t ret = 0;

    if (s->s3 != NULL) {
        ret = s->s3->tmp.finish_md_len;
        if (count > ret)
            count = ret;
        memcpy(buf, s->s3->tmp.finish_md, count);
    }
    return ret;
}

/* return length of latest Finished message we expected, copy to 'buf' */
size_t SSL_get_peer_finished(const SSL *s, void *buf, size_t count)
{
    size_t ret = 0;

    if (s->s3 != NULL) {
        ret = s->s3->tmp.peer_finish_md_len;
        if (count > ret)
            count = ret;
        memcpy(buf, s->s3->tmp.peer_finish_md, count);
    }
    return ret;
}

int SSL_get_verify_mode(const SSL *s)
{
    return s->verify_mode;
}

int SSL_get_verify_depth(const SSL *s)
{
    return X509_VERIFY_PARAM_get_depth(s->param);
}

int (*SSL_get_verify_callback(const SSL *s)) (int, X509_STORE_CTX *) {
    return s->verify_callback;
}

int SSL_CTX_get_verify_mode(const SSL_CTX *ctx)
{
    return ctx->verify_mode;
}

int SSL_CTX_get_verify_depth(const SSL_CTX *ctx)
{
    return X509_VERIFY_PARAM_get_depth(ctx->param);
}

int (*SSL_CTX_get_verify_callback(const SSL_CTX *ctx)) (int, X509_STORE_CTX *) {
    return ctx->default_verify_callback;
}

void SSL_set_verify(SSL *s, int mode,
                    int (*callback) (int ok, X509_STORE_CTX *ctx))
{
    s->verify_mode = mode;
    if (callback != NULL)
        s->verify_callback = callback;
}

void SSL_set_verify_depth(SSL *s, int depth)
{
    X509_VERIFY_PARAM_set_depth(s->param, depth);
}

void SSL_set_read_ahead(SSL *s, int yes)
{
    RECORD_LAYER_set_read_ahead(&s->rlayer, yes);
}

int SSL_get_read_ahead(const SSL *s)
{
    return RECORD_LAYER_get_read_ahead(&s->rlayer);
}

int SSL_pending(const SSL *s)
{
    size_t pending = s->method->ssl_pending(s);

    /*
     * SSL_pending cannot work properly if read-ahead is enabled
     * (SSL_[CTX_]ctrl(..., SSL_CTRL_SET_READ_AHEAD, 1, NULL)), and it is
     * impossible to fix since SSL_pending cannot report errors that may be
     * observed while scanning the new data. (Note that SSL_pending() is
     * often used as a boolean value, so we'd better not return -1.)
     *
     * SSL_pending also cannot work properly if the value >INT_MAX. In that case
     * we just return INT_MAX.
     */
    return pending < INT_MAX ? (int)pending : INT_MAX;
}

int SSL_has_pending(const SSL *s)
{
    /*
     * Similar to SSL_pending() but returns a 1 to indicate that we have
     * unprocessed data available or 0 otherwise (as opposed to the number of
     * bytes available). Unlike SSL_pending() this will take into account
     * read_ahead data. A 1 return simply indicates that we have unprocessed
     * data. That data may not result in any application data, or we may fail
     * to parse the records for some reason.
     */
    if (RECORD_LAYER_processed_read_pending(&s->rlayer))
        return 1;

    return RECORD_LAYER_read_pending(&s->rlayer);
}

X509 *SSL_get_peer_certificate(const SSL *s)
{
    X509 *r;

    if ((s == NULL) || (s->session == NULL))
        r = NULL;
    else
        r = s->session->peer;

    if (r == NULL)
        return r;

    X509_up_ref(r);

    return r;
}

STACK_OF(X509) *SSL_get_peer_cert_chain(const SSL *s)
{
    STACK_OF(X509) *r;

    if ((s == NULL) || (s->session == NULL))
        r = NULL;
    else
        r = s->session->peer_chain;

    /*
     * If we are a client, cert_chain includes the peer's own certificate; if
     * we are a server, it does not.
     */

    return r;
}

/*
 * Now in theory, since the calling process own 't' it should be safe to
 * modify.  We need to be able to read f without being hassled
 */
int SSL_copy_session_id(SSL *t, const SSL *f)
{
    int i;
    /* Do we need to to SSL locking? */
    if (!SSL_set_session(t, SSL_get_session(f))) {
        return 0;
    }

    /*
     * what if we are setup for one protocol version but want to talk another
     */
    if (t->method != f->method) {
        t->method->ssl_free(t);
        t->method = f->method;
        if (t->method->ssl_new(t) == 0)
            return 0;
    }

    CRYPTO_UP_REF(&f->cert->references, &i, f->cert->lock);
    ssl_cert_free(t->cert);
    t->cert = f->cert;
    if (!SSL_set_session_id_context(t, f->sid_ctx, (int)f->sid_ctx_length)) {
        return 0;
    }

    return 1;
}

/* Fix this so it checks all the valid key/cert options */
int SSL_CTX_check_private_key(const SSL_CTX *ctx)
{
    if ((ctx == NULL) || (ctx->cert->key->x509 == NULL)) {
        SSLerr(SSL_F_SSL_CTX_CHECK_PRIVATE_KEY, SSL_R_NO_CERTIFICATE_ASSIGNED);
        return 0;
    }
    if (ctx->cert->key->privatekey == NULL) {
        SSLerr(SSL_F_SSL_CTX_CHECK_PRIVATE_KEY, SSL_R_NO_PRIVATE_KEY_ASSIGNED);
        return 0;
    }
    return X509_check_private_key
            (ctx->cert->key->x509, ctx->cert->key->privatekey);
}

/* Fix this function so that it takes an optional type parameter */
int SSL_check_private_key(const SSL *ssl)
{
    if (ssl == NULL) {
        SSLerr(SSL_F_SSL_CHECK_PRIVATE_KEY, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    if (ssl->cert->key->x509 == NULL) {
        SSLerr(SSL_F_SSL_CHECK_PRIVATE_KEY, SSL_R_NO_CERTIFICATE_ASSIGNED);
        return 0;
    }
    if (ssl->cert->key->privatekey == NULL) {
        SSLerr(SSL_F_SSL_CHECK_PRIVATE_KEY, SSL_R_NO_PRIVATE_KEY_ASSIGNED);
        return 0;
    }
    return X509_check_private_key(ssl->cert->key->x509,
                                   ssl->cert->key->privatekey);
}

int SSL_waiting_for_async(SSL *s)
{
    if (s->job)
        return 1;

    return 0;
}

int SSL_get_all_async_fds(SSL *s, OSSL_ASYNC_FD *fds, size_t *numfds)
{
    ASYNC_WAIT_CTX *ctx = s->waitctx;

    if (ctx == NULL)
        return 0;
    return ASYNC_WAIT_CTX_get_all_fds(ctx, fds, numfds);
}

int SSL_get_changed_async_fds(SSL *s, OSSL_ASYNC_FD *addfd, size_t *numaddfds,
                              OSSL_ASYNC_FD *delfd, size_t *numdelfds)
{
    ASYNC_WAIT_CTX *ctx = s->waitctx;

    if (ctx == NULL)
        return 0;
    return ASYNC_WAIT_CTX_get_changed_fds(ctx, addfd, numaddfds, delfd,
                                          numdelfds);
}

int SSL_accept(SSL *s)
{
    if (s->handshake_func == NULL) {
        /* Not properly initialized yet */
        SSL_set_accept_state(s);
    }

    return SSL_do_handshake(s);
}

int SSL_connect(SSL *s)
{
    if (s->handshake_func == NULL) {
        /* Not properly initialized yet */
        SSL_set_connect_state(s);
    }

    return SSL_do_handshake(s);
}

long SSL_get_default_timeout(const SSL *s)
{
    return s->method->get_timeout();
}

static int ssl_start_async_job(SSL *s, struct ssl_async_args *args,
                               int (*func) (void *))
{
    int ret;
    if (s->waitctx == NULL) {
        s->waitctx = ASYNC_WAIT_CTX_new();
        if (s->waitctx == NULL)
            return -1;
    }
    switch (ASYNC_start_job(&s->job, s->waitctx, &ret, func, args,
                            sizeof(struct ssl_async_args))) {
    case ASYNC_ERR:
        s->rwstate = SSL_NOTHING;
        SSLerr(SSL_F_SSL_START_ASYNC_JOB, SSL_R_FAILED_TO_INIT_ASYNC);
        return -1;
    case ASYNC_PAUSE:
        s->rwstate = SSL_ASYNC_PAUSED;
        return -1;
    case ASYNC_NO_JOBS:
        s->rwstate = SSL_ASYNC_NO_JOBS;
        return -1;
    case ASYNC_FINISH:
        s->job = NULL;
        return ret;
    default:
        s->rwstate = SSL_NOTHING;
        SSLerr(SSL_F_SSL_START_ASYNC_JOB, ERR_R_INTERNAL_ERROR);
        /* Shouldn't happen */
        return -1;
    }
}

static int ssl_io_intern(void *vargs)
{
    struct ssl_async_args *args;
    SSL *s;
    void *buf;
    size_t num;

    args = (struct ssl_async_args *)vargs;
    s = args->s;
    buf = args->buf;
    num = args->num;
    switch (args->type) {
    case READFUNC:
        return args->f.func_read(s, buf, num, &s->asyncrw);
    case WRITEFUNC:
        return args->f.func_write(s, buf, num, &s->asyncrw);
    case OTHERFUNC:
        return args->f.func_other(s);
    }
    return -1;
}

int ssl_read_internal(SSL *s, void *buf, size_t num, size_t *readbytes)
{
    if (s->handshake_func == NULL) {
        SSLerr(SSL_F_SSL_READ_INTERNAL, SSL_R_UNINITIALIZED);
        return -1;
    }

    if (s->shutdown & SSL_RECEIVED_SHUTDOWN) {
        s->rwstate = SSL_NOTHING;
        return 0;
    }

    if (s->early_data_state == SSL_EARLY_DATA_CONNECT_RETRY
                || s->early_data_state == SSL_EARLY_DATA_ACCEPT_RETRY) {
        SSLerr(SSL_F_SSL_READ_INTERNAL, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
        return 0;
    }
    /*
     * If we are a client and haven't received the ServerHello etc then we
     * better do that
     */
    ossl_statem_check_finish_init(s, 0);

    if ((s->mode & SSL_MODE_ASYNC) && ASYNC_get_current_job() == NULL) {
        struct ssl_async_args args;
        int ret;

        args.s = s;
        args.buf = buf;
        args.num = num;
        args.type = READFUNC;
        args.f.func_read = s->method->ssl_read;

        ret = ssl_start_async_job(s, &args, ssl_io_intern);
        *readbytes = s->asyncrw;
        return ret;
    } else {
        return s->method->ssl_read(s, buf, num, readbytes);
    }
}

int SSL_read(SSL *s, void *buf, int num)
{
    int ret;
    size_t readbytes;

    if (num < 0) {
        SSLerr(SSL_F_SSL_READ, SSL_R_BAD_LENGTH);
        return -1;
    }

    ret = ssl_read_internal(s, buf, (size_t)num, &readbytes);

    /*
     * The cast is safe here because ret should be <= INT_MAX because num is
     * <= INT_MAX
     */
    if (ret > 0)
        ret = (int)readbytes;

    return ret;
}

int SSL_read_ex(SSL *s, void *buf, size_t num, size_t *readbytes)
{
    int ret = ssl_read_internal(s, buf, num, readbytes);

    if (ret < 0)
        ret = 0;
    return ret;
}

int SSL_read_early_data(SSL *s, void *buf, size_t num, size_t *readbytes)
{
    int ret;

    if (!s->server) {
        SSLerr(SSL_F_SSL_READ_EARLY_DATA, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
        return SSL_READ_EARLY_DATA_ERROR;
    }

    switch (s->early_data_state) {
    case SSL_EARLY_DATA_NONE:
        if (!SSL_in_before(s)) {
            SSLerr(SSL_F_SSL_READ_EARLY_DATA,
                   ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
            return SSL_READ_EARLY_DATA_ERROR;
        }
        /* fall through */

    case SSL_EARLY_DATA_ACCEPT_RETRY:
        s->early_data_state = SSL_EARLY_DATA_ACCEPTING;
        ret = SSL_accept(s);
        if (ret <= 0) {
            /* NBIO or error */
            s->early_data_state = SSL_EARLY_DATA_ACCEPT_RETRY;
            return SSL_READ_EARLY_DATA_ERROR;
        }
        /* fall through */

    case SSL_EARLY_DATA_READ_RETRY:
        if (s->ext.early_data == SSL_EARLY_DATA_ACCEPTED) {
            s->early_data_state = SSL_EARLY_DATA_READING;
            ret = SSL_read_ex(s, buf, num, readbytes);
            /*
             * State machine will update early_data_state to
             * SSL_EARLY_DATA_FINISHED_READING if we get an EndOfEarlyData
             * message
             */
            if (ret > 0 || (ret <= 0 && s->early_data_state
                                        != SSL_EARLY_DATA_FINISHED_READING)) {
                s->early_data_state = SSL_EARLY_DATA_READ_RETRY;
                return ret > 0 ? SSL_READ_EARLY_DATA_SUCCESS
                               : SSL_READ_EARLY_DATA_ERROR;
            }
        } else {
            s->early_data_state = SSL_EARLY_DATA_FINISHED_READING;
        }
        *readbytes = 0;
        return SSL_READ_EARLY_DATA_FINISH;

    default:
        SSLerr(SSL_F_SSL_READ_EARLY_DATA, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
        return SSL_READ_EARLY_DATA_ERROR;
    }
}

int SSL_get_early_data_status(const SSL *s)
{
    return s->ext.early_data;
}

static int ssl_peek_internal(SSL *s, void *buf, size_t num, size_t *readbytes)
{
    if (s->handshake_func == NULL) {
        SSLerr(SSL_F_SSL_PEEK_INTERNAL, SSL_R_UNINITIALIZED);
        return -1;
    }

    if (s->shutdown & SSL_RECEIVED_SHUTDOWN) {
        return 0;
    }
    if ((s->mode & SSL_MODE_ASYNC) && ASYNC_get_current_job() == NULL) {
        struct ssl_async_args args;
        int ret;

        args.s = s;
        args.buf = buf;
        args.num = num;
        args.type = READFUNC;
        args.f.func_read = s->method->ssl_peek;

        ret = ssl_start_async_job(s, &args, ssl_io_intern);
        *readbytes = s->asyncrw;
        return ret;
    } else {
        return s->method->ssl_peek(s, buf, num, readbytes);
    }
}

int SSL_peek(SSL *s, void *buf, int num)
{
    int ret;
    size_t readbytes;

    if (num < 0) {
        SSLerr(SSL_F_SSL_PEEK, SSL_R_BAD_LENGTH);
        return -1;
    }

    ret = ssl_peek_internal(s, buf, (size_t)num, &readbytes);

    /*
     * The cast is safe here because ret should be <= INT_MAX because num is
     * <= INT_MAX
     */
    if (ret > 0)
        ret = (int)readbytes;

    return ret;
}


int SSL_peek_ex(SSL *s, void *buf, size_t num, size_t *readbytes)
{
    int ret = ssl_peek_internal(s, buf, num, readbytes);

    if (ret < 0)
        ret = 0;
    return ret;
}

int ssl_write_internal(SSL *s, const void *buf, size_t num, size_t *written)
{
    if (s->handshake_func == NULL) {
        SSLerr(SSL_F_SSL_WRITE_INTERNAL, SSL_R_UNINITIALIZED);
        return -1;
    }

    if (s->shutdown & SSL_SENT_SHUTDOWN) {
        s->rwstate = SSL_NOTHING;
        SSLerr(SSL_F_SSL_WRITE_INTERNAL, SSL_R_PROTOCOL_IS_SHUTDOWN);
        return -1;
    }

    if (s->early_data_state == SSL_EARLY_DATA_CONNECT_RETRY
                || s->early_data_state == SSL_EARLY_DATA_ACCEPT_RETRY
                || s->early_data_state == SSL_EARLY_DATA_READ_RETRY) {
        SSLerr(SSL_F_SSL_WRITE_INTERNAL, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
        return 0;
    }
    /* If we are a client and haven't sent the Finished we better do that */
    ossl_statem_check_finish_init(s, 1);

    if ((s->mode & SSL_MODE_ASYNC) && ASYNC_get_current_job() == NULL) {
        int ret;
        struct ssl_async_args args;

        args.s = s;
        args.buf = (void *)buf;
        args.num = num;
        args.type = WRITEFUNC;
        args.f.func_write = s->method->ssl_write;

        ret = ssl_start_async_job(s, &args, ssl_io_intern);
        *written = s->asyncrw;
        return ret;
    } else {
        return s->method->ssl_write(s, buf, num, written);
    }
}

int SSL_write(SSL *s, const void *buf, int num)
{
    int ret;
    size_t written;

    if (num < 0) {
        SSLerr(SSL_F_SSL_WRITE, SSL_R_BAD_LENGTH);
        return -1;
    }

    ret = ssl_write_internal(s, buf, (size_t)num, &written);

    /*
     * The cast is safe here because ret should be <= INT_MAX because num is
     * <= INT_MAX
     */
    if (ret > 0)
        ret = (int)written;

    return ret;
}

int SSL_write_ex(SSL *s, const void *buf, size_t num, size_t *written)
{
    int ret = ssl_write_internal(s, buf, num, written);

    if (ret < 0)
        ret = 0;
    return ret;
}

int SSL_write_early_data(SSL *s, const void *buf, size_t num, size_t *written)
{
    int ret, early_data_state;
    size_t writtmp;
    uint32_t partialwrite;

    switch (s->early_data_state) {
    case SSL_EARLY_DATA_NONE:
        if (s->server
                || !SSL_in_before(s)
                || ((s->session == NULL || s->session->ext.max_early_data == 0)
                     && (s->psk_use_session_cb == NULL))) {
            SSLerr(SSL_F_SSL_WRITE_EARLY_DATA,
                   ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
            return 0;
        }
        /* fall through */

    case SSL_EARLY_DATA_CONNECT_RETRY:
        s->early_data_state = SSL_EARLY_DATA_CONNECTING;
        ret = SSL_connect(s);
        if (ret <= 0) {
            /* NBIO or error */
            s->early_data_state = SSL_EARLY_DATA_CONNECT_RETRY;
            return 0;
        }
        /* fall through */

    case SSL_EARLY_DATA_WRITE_RETRY:
        s->early_data_state = SSL_EARLY_DATA_WRITING;
        /*
         * We disable partial write for early data because we don't keep track
         * of how many bytes we've written between the SSL_write_ex() call and
         * the flush if the flush needs to be retried)
         */
        partialwrite = s->mode & SSL_MODE_ENABLE_PARTIAL_WRITE;
        s->mode &= ~SSL_MODE_ENABLE_PARTIAL_WRITE;
        ret = SSL_write_ex(s, buf, num, &writtmp);
        s->mode |= partialwrite;
        if (!ret) {
            s->early_data_state = SSL_EARLY_DATA_WRITE_RETRY;
            return ret;
        }
        s->early_data_state = SSL_EARLY_DATA_WRITE_FLUSH;
        /* fall through */

    case SSL_EARLY_DATA_WRITE_FLUSH:
        /* The buffering BIO is still in place so we need to flush it */
        if (statem_flush(s) != 1)
            return 0;
        *written = num;
        s->early_data_state = SSL_EARLY_DATA_WRITE_RETRY;
        return 1;

    case SSL_EARLY_DATA_FINISHED_READING:
    case SSL_EARLY_DATA_READ_RETRY:
        early_data_state = s->early_data_state;
        /* We are a server writing to an unauthenticated client */
        s->early_data_state = SSL_EARLY_DATA_UNAUTH_WRITING;
        ret = SSL_write_ex(s, buf, num, written);
        /* The buffering BIO is still in place */
        if (ret)
            (void)BIO_flush(s->wbio);
        s->early_data_state = early_data_state;
        return ret;

    default:
        SSLerr(SSL_F_SSL_WRITE_EARLY_DATA, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
        return 0;
    }
}

int SSL_shutdown(SSL *s)
{
    /*
     * Note that this function behaves differently from what one might
     * expect.  Return values are 0 for no success (yet), 1 for success; but
     * calling it once is usually not enough, even if blocking I/O is used
     * (see ssl3_shutdown).
     */

    if (s->handshake_func == NULL) {
        SSLerr(SSL_F_SSL_SHUTDOWN, SSL_R_UNINITIALIZED);
        return -1;
    }

    if (!SSL_in_init(s)) {
        if ((s->mode & SSL_MODE_ASYNC) && ASYNC_get_current_job() == NULL) {
            struct ssl_async_args args;

            args.s = s;
            args.type = OTHERFUNC;
            args.f.func_other = s->method->ssl_shutdown;

            return ssl_start_async_job(s, &args, ssl_io_intern);
        } else {
            return s->method->ssl_shutdown(s);
        }
    } else {
        SSLerr(SSL_F_SSL_SHUTDOWN, SSL_R_SHUTDOWN_WHILE_IN_INIT);
        return -1;
    }
}

int SSL_key_update(SSL *s, int updatetype)
{
    /*
     * TODO(TLS1.3): How will applications know whether TLSv1.3 has been
     * negotiated, and that it is appropriate to call SSL_key_update() instead
     * of SSL_renegotiate().
     */
    if (!SSL_IS_TLS13(s)) {
        SSLerr(SSL_F_SSL_KEY_UPDATE, SSL_R_WRONG_SSL_VERSION);
        return 0;
    }

    if (updatetype != SSL_KEY_UPDATE_NOT_REQUESTED
            && updatetype != SSL_KEY_UPDATE_REQUESTED) {
        SSLerr(SSL_F_SSL_KEY_UPDATE, SSL_R_INVALID_KEY_UPDATE_TYPE);
        return 0;
    }

    if (!SSL_is_init_finished(s)) {
        SSLerr(SSL_F_SSL_KEY_UPDATE, SSL_R_STILL_IN_INIT);
        return 0;
    }

    ossl_statem_set_in_init(s, 1);
    s->key_update = updatetype;
    return 1;
}

int SSL_get_key_update_type(const SSL *s)
{
    return s->key_update;
}

int SSL_renegotiate(SSL *s)
{
    if (SSL_IS_TLS13(s)) {
        SSLerr(SSL_F_SSL_RENEGOTIATE, SSL_R_WRONG_SSL_VERSION);
        return 0;
    }

    if ((s->options & SSL_OP_NO_RENEGOTIATION)) {
        SSLerr(SSL_F_SSL_RENEGOTIATE, SSL_R_NO_RENEGOTIATION);
        return 0;
    }

    s->renegotiate = 1;
    s->new_session = 1;

    return s->method->ssl_renegotiate(s);
}

int SSL_renegotiate_abbreviated(SSL *s)
{
    if (SSL_IS_TLS13(s)) {
        SSLerr(SSL_F_SSL_RENEGOTIATE_ABBREVIATED, SSL_R_WRONG_SSL_VERSION);
        return 0;
    }

    if ((s->options & SSL_OP_NO_RENEGOTIATION)) {
        SSLerr(SSL_F_SSL_RENEGOTIATE_ABBREVIATED, SSL_R_NO_RENEGOTIATION);
        return 0;
    }

    s->renegotiate = 1;
    s->new_session = 0;

    return s->method->ssl_renegotiate(s);
}

int SSL_renegotiate_pending(const SSL *s)
{
    /*
     * becomes true when negotiation is requested; false again once a
     * handshake has finished
     */
    return (s->renegotiate != 0);
}

long SSL_ctrl(SSL *s, int cmd, long larg, void *parg)
{
    long l;

    switch (cmd) {
    case SSL_CTRL_GET_READ_AHEAD:
        return RECORD_LAYER_get_read_ahead(&s->rlayer);
    case SSL_CTRL_SET_READ_AHEAD:
        l = RECORD_LAYER_get_read_ahead(&s->rlayer);
        RECORD_LAYER_set_read_ahead(&s->rlayer, larg);
        return l;

    case SSL_CTRL_SET_MSG_CALLBACK_ARG:
        s->msg_callback_arg = parg;
        return 1;

    case SSL_CTRL_MODE:
        return (s->mode |= larg);
    case SSL_CTRL_CLEAR_MODE:
        return (s->mode &= ~larg);
    case SSL_CTRL_GET_MAX_CERT_LIST:
        return (long)s->max_cert_list;
    case SSL_CTRL_SET_MAX_CERT_LIST:
        if (larg < 0)
            return 0;
        l = (long)s->max_cert_list;
        s->max_cert_list = (size_t)larg;
        return l;
    case SSL_CTRL_SET_MAX_SEND_FRAGMENT:
        if (larg < 512 || larg > SSL3_RT_MAX_PLAIN_LENGTH)
            return 0;
        s->max_send_fragment = larg;
        if (s->max_send_fragment < s->split_send_fragment)
            s->split_send_fragment = s->max_send_fragment;
        return 1;
    case SSL_CTRL_SET_SPLIT_SEND_FRAGMENT:
        if ((size_t)larg > s->max_send_fragment || larg == 0)
            return 0;
        s->split_send_fragment = larg;
        return 1;
    case SSL_CTRL_SET_MAX_PIPELINES:
        if (larg < 1 || larg > SSL_MAX_PIPELINES)
            return 0;
        s->max_pipelines = larg;
        if (larg > 1)
            RECORD_LAYER_set_read_ahead(&s->rlayer, 1);
        return 1;
    case SSL_CTRL_GET_RI_SUPPORT:
        if (s->s3)
            return s->s3->send_connection_binding;
        else
            return 0;
    case SSL_CTRL_CERT_FLAGS:
        return (s->cert->cert_flags |= larg);
    case SSL_CTRL_CLEAR_CERT_FLAGS:
        return (s->cert->cert_flags &= ~larg);

    case SSL_CTRL_GET_RAW_CIPHERLIST:
        if (parg) {
            if (s->s3->tmp.ciphers_raw == NULL)
                return 0;
            *(unsigned char **)parg = s->s3->tmp.ciphers_raw;
            return (int)s->s3->tmp.ciphers_rawlen;
        } else {
            return TLS_CIPHER_LEN;
        }
    case SSL_CTRL_GET_EXTMS_SUPPORT:
        if (!s->session || SSL_in_init(s) || ossl_statem_get_in_handshake(s))
            return -1;
        if (s->session->flags & SSL_SESS_FLAG_EXTMS)
            return 1;
        else
            return 0;
    case SSL_CTRL_SET_MIN_PROTO_VERSION:
        return ssl_check_allowed_versions(larg, s->max_proto_version)
               && ssl_set_version_bound(s->ctx->method->version, (int)larg,
                                        &s->min_proto_version);
    case SSL_CTRL_GET_MIN_PROTO_VERSION:
        return s->min_proto_version;
    case SSL_CTRL_SET_MAX_PROTO_VERSION:
        return ssl_check_allowed_versions(s->min_proto_version, larg)
               && ssl_set_version_bound(s->ctx->method->version, (int)larg,
                                        &s->max_proto_version);
    case SSL_CTRL_GET_MAX_PROTO_VERSION:
        return s->max_proto_version;
    default:
        return s->method->ssl_ctrl(s, cmd, larg, parg);
    }
}

long SSL_callback_ctrl(SSL *s, int cmd, void (*fp) (void))
{
    switch (cmd) {
    case SSL_CTRL_SET_MSG_CALLBACK:
        s->msg_callback = (void (*)
                           (int write_p, int version, int content_type,
                            const void *buf, size_t len, SSL *ssl,
                            void *arg))(fp);
        return 1;

    default:
        return s->method->ssl_callback_ctrl(s, cmd, fp);
    }
}

LHASH_OF(SSL_SESSION) *SSL_CTX_sessions(SSL_CTX *ctx)
{
    return ctx->sessions;
}

long SSL_CTX_ctrl(SSL_CTX *ctx, int cmd, long larg, void *parg)
{
    long l;
    /* For some cases with ctx == NULL perform syntax checks */
    if (ctx == NULL) {
        switch (cmd) {
#ifndef OPENSSL_NO_EC
        case SSL_CTRL_SET_GROUPS_LIST:
            return tls1_set_groups_list(NULL, NULL, parg);
#endif
        case SSL_CTRL_SET_SIGALGS_LIST:
        case SSL_CTRL_SET_CLIENT_SIGALGS_LIST:
            return tls1_set_sigalgs_list(NULL, parg, 0);
        default:
            return 0;
        }
    }

    switch (cmd) {
    case SSL_CTRL_GET_READ_AHEAD:
        return ctx->read_ahead;
    case SSL_CTRL_SET_READ_AHEAD:
        l = ctx->read_ahead;
        ctx->read_ahead = larg;
        return l;

    case SSL_CTRL_SET_MSG_CALLBACK_ARG:
        ctx->msg_callback_arg = parg;
        return 1;

    case SSL_CTRL_GET_MAX_CERT_LIST:
        return (long)ctx->max_cert_list;
    case SSL_CTRL_SET_MAX_CERT_LIST:
        if (larg < 0)
            return 0;
        l = (long)ctx->max_cert_list;
        ctx->max_cert_list = (size_t)larg;
        return l;

    case SSL_CTRL_SET_SESS_CACHE_SIZE:
        if (larg < 0)
            return 0;
        l = (long)ctx->session_cache_size;
        ctx->session_cache_size = (size_t)larg;
        return l;
    case SSL_CTRL_GET_SESS_CACHE_SIZE:
        return (long)ctx->session_cache_size;
    case SSL_CTRL_SET_SESS_CACHE_MODE:
        l = ctx->session_cache_mode;
        ctx->session_cache_mode = larg;
        return l;
    case SSL_CTRL_GET_SESS_CACHE_MODE:
        return ctx->session_cache_mode;

    case SSL_CTRL_SESS_NUMBER:
        return lh_SSL_SESSION_num_items(ctx->sessions);
    case SSL_CTRL_SESS_CONNECT:
        return tsan_load(&ctx->stats.sess_connect);
    case SSL_CTRL_SESS_CONNECT_GOOD:
        return tsan_load(&ctx->stats.sess_connect_good);
    case SSL_CTRL_SESS_CONNECT_RENEGOTIATE:
        return tsan_load(&ctx->stats.sess_connect_renegotiate);
    case SSL_CTRL_SESS_ACCEPT:
        return tsan_load(&ctx->stats.sess_accept);
    case SSL_CTRL_SESS_ACCEPT_GOOD:
        return tsan_load(&ctx->stats.sess_accept_good);
    case SSL_CTRL_SESS_ACCEPT_RENEGOTIATE:
        return tsan_load(&ctx->stats.sess_accept_renegotiate);
    case SSL_CTRL_SESS_HIT:
        return tsan_load(&ctx->stats.sess_hit);
    case SSL_CTRL_SESS_CB_HIT:
        return tsan_load(&ctx->stats.sess_cb_hit);
    case SSL_CTRL_SESS_MISSES:
        return tsan_load(&ctx->stats.sess_miss);
    case SSL_CTRL_SESS_TIMEOUTS:
        return tsan_load(&ctx->stats.sess_timeout);
    case SSL_CTRL_SESS_CACHE_FULL:
        return tsan_load(&ctx->stats.sess_cache_full);
    case SSL_CTRL_MODE:
        return (ctx->mode |= larg);
    case SSL_CTRL_CLEAR_MODE:
        return (ctx->mode &= ~larg);
    case SSL_CTRL_SET_MAX_SEND_FRAGMENT:
        if (larg < 512 || larg > SSL3_RT_MAX_PLAIN_LENGTH)
            return 0;
        ctx->max_send_fragment = larg;
        if (ctx->max_send_fragment < ctx->split_send_fragment)
            ctx->split_send_fragment = ctx->max_send_fragment;
        return 1;
    case SSL_CTRL_SET_SPLIT_SEND_FRAGMENT:
        if ((size_t)larg > ctx->max_send_fragment || larg == 0)
            return 0;
        ctx->split_send_fragment = larg;
        return 1;
    case SSL_CTRL_SET_MAX_PIPELINES:
        if (larg < 1 || larg > SSL_MAX_PIPELINES)
            return 0;
        ctx->max_pipelines = larg;
        return 1;
    case SSL_CTRL_CERT_FLAGS:
        return (ctx->cert->cert_flags |= larg);
    case SSL_CTRL_CLEAR_CERT_FLAGS:
        return (ctx->cert->cert_flags &= ~larg);
    case SSL_CTRL_SET_MIN_PROTO_VERSION:
        return ssl_check_allowed_versions(larg, ctx->max_proto_version)
               && ssl_set_version_bound(ctx->method->version, (int)larg,
                                        &ctx->min_proto_version);
    case SSL_CTRL_GET_MIN_PROTO_VERSION:
        return ctx->min_proto_version;
    case SSL_CTRL_SET_MAX_PROTO_VERSION:
        return ssl_check_allowed_versions(ctx->min_proto_version, larg)
               && ssl_set_version_bound(ctx->method->version, (int)larg,
                                        &ctx->max_proto_version);
    case SSL_CTRL_GET_MAX_PROTO_VERSION:
        return ctx->max_proto_version;
    default:
        return ctx->method->ssl_ctx_ctrl(ctx, cmd, larg, parg);
    }
}

long SSL_CTX_callback_ctrl(SSL_CTX *ctx, int cmd, void (*fp) (void))
{
    switch (cmd) {
    case SSL_CTRL_SET_MSG_CALLBACK:
        ctx->msg_callback = (void (*)
                             (int write_p, int version, int content_type,
                              const void *buf, size_t len, SSL *ssl,
                              void *arg))(fp);
        return 1;

    default:
        return ctx->method->ssl_ctx_callback_ctrl(ctx, cmd, fp);
    }
}

int ssl_cipher_id_cmp(const SSL_CIPHER *a, const SSL_CIPHER *b)
{
    if (a->id > b->id)
        return 1;
    if (a->id < b->id)
        return -1;
    return 0;
}

int ssl_cipher_ptr_id_cmp(const SSL_CIPHER *const *ap,
                          const SSL_CIPHER *const *bp)
{
    if ((*ap)->id > (*bp)->id)
        return 1;
    if ((*ap)->id < (*bp)->id)
        return -1;
    return 0;
}

/** return a STACK of the ciphers available for the SSL and in order of
 * preference */
STACK_OF(SSL_CIPHER) *SSL_get_ciphers(const SSL *s)
{
    if (s != NULL) {
        if (s->cipher_list != NULL) {
            return s->cipher_list;
        } else if ((s->ctx != NULL) && (s->ctx->cipher_list != NULL)) {
            return s->ctx->cipher_list;
        }
    }
    return NULL;
}

STACK_OF(SSL_CIPHER) *SSL_get_client_ciphers(const SSL *s)
{
    if ((s == NULL) || (s->session == NULL) || !s->server)
        return NULL;
    return s->session->ciphers;
}

STACK_OF(SSL_CIPHER) *SSL_get1_supported_ciphers(SSL *s)
{
    STACK_OF(SSL_CIPHER) *sk = NULL, *ciphers;
    int i;

    ciphers = SSL_get_ciphers(s);
    if (!ciphers)
        return NULL;
    if (!ssl_set_client_disabled(s))
        return NULL;
    for (i = 0; i < sk_SSL_CIPHER_num(ciphers); i++) {
        const SSL_CIPHER *c = sk_SSL_CIPHER_value(ciphers, i);
        if (!ssl_cipher_disabled(s, c, SSL_SECOP_CIPHER_SUPPORTED, 0)) {
            if (!sk)
                sk = sk_SSL_CIPHER_new_null();
            if (!sk)
                return NULL;
            if (!sk_SSL_CIPHER_push(sk, c)) {
                sk_SSL_CIPHER_free(sk);
                return NULL;
            }
        }
    }
    return sk;
}

/** return a STACK of the ciphers available for the SSL and in order of
 * algorithm id */
STACK_OF(SSL_CIPHER) *ssl_get_ciphers_by_id(SSL *s)
{
    if (s != NULL) {
        if (s->cipher_list_by_id != NULL) {
            return s->cipher_list_by_id;
        } else if ((s->ctx != NULL) && (s->ctx->cipher_list_by_id != NULL)) {
            return s->ctx->cipher_list_by_id;
        }
    }
    return NULL;
}

/** The old interface to get the same thing as SSL_get_ciphers() */
const char *SSL_get_cipher_list(const SSL *s, int n)
{
    const SSL_CIPHER *c;
    STACK_OF(SSL_CIPHER) *sk;

    if (s == NULL)
        return NULL;
    sk = SSL_get_ciphers(s);
    if ((sk == NULL) || (sk_SSL_CIPHER_num(sk) <= n))
        return NULL;
    c = sk_SSL_CIPHER_value(sk, n);
    if (c == NULL)
        return NULL;
    return c->name;
}

/** return a STACK of the ciphers available for the SSL_CTX and in order of
 * preference */
STACK_OF(SSL_CIPHER) *SSL_CTX_get_ciphers(const SSL_CTX *ctx)
{
    if (ctx != NULL)
        return ctx->cipher_list;
    return NULL;
}

/*
 * Distinguish between ciphers controlled by set_ciphersuite() and
 * set_cipher_list() when counting.
 */
static int cipher_list_tls12_num(STACK_OF(SSL_CIPHER) *sk)
{
    int i, num = 0;
    const SSL_CIPHER *c;

    if (sk == NULL)
        return 0;
    for (i = 0; i < sk_SSL_CIPHER_num(sk); ++i) {
        c = sk_SSL_CIPHER_value(sk, i);
        if (c->min_tls >= TLS1_3_VERSION)
            continue;
        num++;
    }
    return num;
}

/** specify the ciphers to be used by default by the SSL_CTX */
int SSL_CTX_set_cipher_list(SSL_CTX *ctx, const char *str)
{
    STACK_OF(SSL_CIPHER) *sk;

    sk = ssl_create_cipher_list(ctx->method, ctx->tls13_ciphersuites,
                                &ctx->cipher_list, &ctx->cipher_list_by_id, str,
                                ctx->cert);
    /*
     * ssl_create_cipher_list may return an empty stack if it was unable to
     * find a cipher matching the given rule string (for example if the rule
     * string specifies a cipher which has been disabled). This is not an
     * error as far as ssl_create_cipher_list is concerned, and hence
     * ctx->cipher_list and ctx->cipher_list_by_id has been updated.
     */
    if (sk == NULL)
        return 0;
    else if (cipher_list_tls12_num(sk) == 0) {
        SSLerr(SSL_F_SSL_CTX_SET_CIPHER_LIST, SSL_R_NO_CIPHER_MATCH);
        return 0;
    }
    return 1;
}

/** specify the ciphers to be used by the SSL */
int SSL_set_cipher_list(SSL *s, const char *str)
{
    STACK_OF(SSL_CIPHER) *sk;

    sk = ssl_create_cipher_list(s->ctx->method, s->tls13_ciphersuites,
                                &s->cipher_list, &s->cipher_list_by_id, str,
                                s->cert);
    /* see comment in SSL_CTX_set_cipher_list */
    if (sk == NULL)
        return 0;
    else if (cipher_list_tls12_num(sk) == 0) {
        SSLerr(SSL_F_SSL_SET_CIPHER_LIST, SSL_R_NO_CIPHER_MATCH);
        return 0;
    }
    return 1;
}

char *SSL_get_shared_ciphers(const SSL *s, char *buf, int size)
{
    char *p;
    STACK_OF(SSL_CIPHER) *clntsk, *srvrsk;
    const SSL_CIPHER *c;
    int i;

    if (!s->server
            || s->session == NULL
            || s->session->ciphers == NULL
            || size < 2)
        return NULL;

    p = buf;
    clntsk = s->session->ciphers;
    srvrsk = SSL_get_ciphers(s);
    if (clntsk == NULL || srvrsk == NULL)
        return NULL;

    if (sk_SSL_CIPHER_num(clntsk) == 0 || sk_SSL_CIPHER_num(srvrsk) == 0)
        return NULL;

    for (i = 0; i < sk_SSL_CIPHER_num(clntsk); i++) {
        int n;

        c = sk_SSL_CIPHER_value(clntsk, i);
        if (sk_SSL_CIPHER_find(srvrsk, c) < 0)
            continue;

        n = strlen(c->name);
        if (n + 1 > size) {
            if (p != buf)
                --p;
            *p = '\0';
            return buf;
        }
        strcpy(p, c->name);
        p += n;
        *(p++) = ':';
        size -= n + 1;
    }
    p[-1] = '\0';
    return buf;
}

/** return a servername extension value if provided in Client Hello, or NULL.
 * So far, only host_name types are defined (RFC 3546).
 */

const char *SSL_get_servername(const SSL *s, const int type)
{
    if (type != TLSEXT_NAMETYPE_host_name)
        return NULL;

    /*
     * SNI is not negotiated in pre-TLS-1.3 resumption flows, so fake up an
     * SNI value to return if we are resuming/resumed.  N.B. that we still
     * call the relevant callbacks for such resumption flows, and callbacks
     * might error out if there is not a SNI value available.
     */
    if (s->hit)
        return s->session->ext.hostname;
    return s->ext.hostname;
}

int SSL_get_servername_type(const SSL *s)
{
    if (s->session
        && (!s->ext.hostname ? s->session->
            ext.hostname : s->ext.hostname))
        return TLSEXT_NAMETYPE_host_name;
    return -1;
}

/*
 * SSL_select_next_proto implements the standard protocol selection. It is
 * expected that this function is called from the callback set by
 * SSL_CTX_set_next_proto_select_cb. The protocol data is assumed to be a
 * vector of 8-bit, length prefixed byte strings. The length byte itself is
 * not included in the length. A byte string of length 0 is invalid. No byte
 * string may be truncated. The current, but experimental algorithm for
 * selecting the protocol is: 1) If the server doesn't support NPN then this
 * is indicated to the callback. In this case, the client application has to
 * abort the connection or have a default application level protocol. 2) If
 * the server supports NPN, but advertises an empty list then the client
 * selects the first protocol in its list, but indicates via the API that this
 * fallback case was enacted. 3) Otherwise, the client finds the first
 * protocol in the server's list that it supports and selects this protocol.
 * This is because it's assumed that the server has better information about
 * which protocol a client should use. 4) If the client doesn't support any
 * of the server's advertised protocols, then this is treated the same as
 * case 2. It returns either OPENSSL_NPN_NEGOTIATED if a common protocol was
 * found, or OPENSSL_NPN_NO_OVERLAP if the fallback case was reached.
 */
int SSL_select_next_proto(unsigned char **out, unsigned char *outlen,
                          const unsigned char *server,
                          unsigned int server_len,
                          const unsigned char *client, unsigned int client_len)
{
    unsigned int i, j;
    const unsigned char *result;
    int status = OPENSSL_NPN_UNSUPPORTED;

    /*
     * For each protocol in server preference order, see if we support it.
     */
    for (i = 0; i < server_len;) {
        for (j = 0; j < client_len;) {
            if (server[i] == client[j] &&
                memcmp(&server[i + 1], &client[j + 1], server[i]) == 0) {
                /* We found a match */
                result = &server[i];
                status = OPENSSL_NPN_NEGOTIATED;
                goto found;
            }
            j += client[j];
            j++;
        }
        i += server[i];
        i++;
    }

    /* There's no overlap between our protocols and the server's list. */
    result = client;
    status = OPENSSL_NPN_NO_OVERLAP;

 found:
    *out = (unsigned char *)result + 1;
    *outlen = result[0];
    return status;
}

#ifndef OPENSSL_NO_NEXTPROTONEG
/*
 * SSL_get0_next_proto_negotiated sets *data and *len to point to the
 * client's requested protocol for this connection and returns 0. If the
 * client didn't request any protocol, then *data is set to NULL. Note that
 * the client can request any protocol it chooses. The value returned from
 * this function need not be a member of the list of supported protocols
 * provided by the callback.
 */
void SSL_get0_next_proto_negotiated(const SSL *s, const unsigned char **data,
                                    unsigned *len)
{
    *data = s->ext.npn;
    if (!*data) {
        *len = 0;
    } else {
        *len = (unsigned int)s->ext.npn_len;
    }
}

/*
 * SSL_CTX_set_npn_advertised_cb sets a callback that is called when
 * a TLS server needs a list of supported protocols for Next Protocol
 * Negotiation. The returned list must be in wire format.  The list is
 * returned by setting |out| to point to it and |outlen| to its length. This
 * memory will not be modified, but one should assume that the SSL* keeps a
 * reference to it. The callback should return SSL_TLSEXT_ERR_OK if it
 * wishes to advertise. Otherwise, no such extension will be included in the
 * ServerHello.
 */
void SSL_CTX_set_npn_advertised_cb(SSL_CTX *ctx,
                                   SSL_CTX_npn_advertised_cb_func cb,
                                   void *arg)
{
    ctx->ext.npn_advertised_cb = cb;
    ctx->ext.npn_advertised_cb_arg = arg;
}

/*
 * SSL_CTX_set_next_proto_select_cb sets a callback that is called when a
 * client needs to select a protocol from the server's provided list. |out|
 * must be set to point to the selected protocol (which may be within |in|).
 * The length of the protocol name must be written into |outlen|. The
 * server's advertised protocols are provided in |in| and |inlen|. The
 * callback can assume that |in| is syntactically valid. The client must
 * select a protocol. It is fatal to the connection if this callback returns
 * a value other than SSL_TLSEXT_ERR_OK.
 */
void SSL_CTX_set_npn_select_cb(SSL_CTX *ctx,
                               SSL_CTX_npn_select_cb_func cb,
                               void *arg)
{
    ctx->ext.npn_select_cb = cb;
    ctx->ext.npn_select_cb_arg = arg;
}
#endif

/*
 * SSL_CTX_set_alpn_protos sets the ALPN protocol list on |ctx| to |protos|.
 * |protos| must be in wire-format (i.e. a series of non-empty, 8-bit
 * length-prefixed strings). Returns 0 on success.
 */
int SSL_CTX_set_alpn_protos(SSL_CTX *ctx, const unsigned char *protos,
                            unsigned int protos_len)
{
    OPENSSL_free(ctx->ext.alpn);
    ctx->ext.alpn = OPENSSL_memdup(protos, protos_len);
    if (ctx->ext.alpn == NULL) {
        SSLerr(SSL_F_SSL_CTX_SET_ALPN_PROTOS, ERR_R_MALLOC_FAILURE);
        return 1;
    }
    ctx->ext.alpn_len = protos_len;

    return 0;
}

/*
 * SSL_set_alpn_protos sets the ALPN protocol list on |ssl| to |protos|.
 * |protos| must be in wire-format (i.e. a series of non-empty, 8-bit
 * length-prefixed strings). Returns 0 on success.
 */
int SSL_set_alpn_protos(SSL *ssl, const unsigned char *protos,
                        unsigned int protos_len)
{
    OPENSSL_free(ssl->ext.alpn);
    ssl->ext.alpn = OPENSSL_memdup(protos, protos_len);
    if (ssl->ext.alpn == NULL) {
        SSLerr(SSL_F_SSL_SET_ALPN_PROTOS, ERR_R_MALLOC_FAILURE);
        return 1;
    }
    ssl->ext.alpn_len = protos_len;

    return 0;
}

/*
 * SSL_CTX_set_alpn_select_cb sets a callback function on |ctx| that is
 * called during ClientHello processing in order to select an ALPN protocol
 * from the client's list of offered protocols.
 */
void SSL_CTX_set_alpn_select_cb(SSL_CTX *ctx,
                                SSL_CTX_alpn_select_cb_func cb,
                                void *arg)
{
    ctx->ext.alpn_select_cb = cb;
    ctx->ext.alpn_select_cb_arg = arg;
}

/*
 * SSL_get0_alpn_selected gets the selected ALPN protocol (if any) from |ssl|.
 * On return it sets |*data| to point to |*len| bytes of protocol name
 * (not including the leading length-prefix byte). If the server didn't
 * respond with a negotiated protocol then |*len| will be zero.
 */
void SSL_get0_alpn_selected(const SSL *ssl, const unsigned char **data,
                            unsigned int *len)
{
    *data = NULL;
    if (ssl->s3)
        *data = ssl->s3->alpn_selected;
    if (*data == NULL)
        *len = 0;
    else
        *len = (unsigned int)ssl->s3->alpn_selected_len;
}

int SSL_export_keying_material(SSL *s, unsigned char *out, size_t olen,
                               const char *label, size_t llen,
                               const unsigned char *context, size_t contextlen,
                               int use_context)
{
    if (s->version < TLS1_VERSION && s->version != DTLS1_BAD_VER)
        return -1;

    return s->method->ssl3_enc->export_keying_material(s, out, olen, label,
                                                       llen, context,
                                                       contextlen, use_context);
}

int SSL_export_keying_material_early(SSL *s, unsigned char *out, size_t olen,
                                     const char *label, size_t llen,
                                     const unsigned char *context,
                                     size_t contextlen)
{
    if (s->version != TLS1_3_VERSION)
        return 0;

    return tls13_export_keying_material_early(s, out, olen, label, llen,
                                              context, contextlen);
}

static unsigned long ssl_session_hash(const SSL_SESSION *a)
{
    const unsigned char *session_id = a->session_id;
    unsigned long l;
    unsigned char tmp_storage[4];

    if (a->session_id_length < sizeof(tmp_storage)) {
        memset(tmp_storage, 0, sizeof(tmp_storage));
        memcpy(tmp_storage, a->session_id, a->session_id_length);
        session_id = tmp_storage;
    }

    l = (unsigned long)
        ((unsigned long)session_id[0]) |
        ((unsigned long)session_id[1] << 8L) |
        ((unsigned long)session_id[2] << 16L) |
        ((unsigned long)session_id[3] << 24L);
    return l;
}

/*
 * NB: If this function (or indeed the hash function which uses a sort of
 * coarser function than this one) is changed, ensure
 * SSL_CTX_has_matching_session_id() is checked accordingly. It relies on
 * being able to construct an SSL_SESSION that will collide with any existing
 * session with a matching session ID.
 */
static int ssl_session_cmp(const SSL_SESSION *a, const SSL_SESSION *b)
{
    if (a->ssl_version != b->ssl_version)
        return 1;
    if (a->session_id_length != b->session_id_length)
        return 1;
    return memcmp(a->session_id, b->session_id, a->session_id_length);
}

/*
 * These wrapper functions should remain rather than redeclaring
 * SSL_SESSION_hash and SSL_SESSION_cmp for void* types and casting each
 * variable. The reason is that the functions aren't static, they're exposed
 * via ssl.h.
 */

SSL_CTX *SSL_CTX_new(const SSL_METHOD *meth)
{
    SSL_CTX *ret = NULL;

    if (meth == NULL) {
        SSLerr(SSL_F_SSL_CTX_NEW, SSL_R_NULL_SSL_METHOD_PASSED);
        return NULL;
    }

    if (!OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS, NULL))
        return NULL;

    if (SSL_get_ex_data_X509_STORE_CTX_idx() < 0) {
        SSLerr(SSL_F_SSL_CTX_NEW, SSL_R_X509_VERIFICATION_SETUP_PROBLEMS);
        goto err;
    }
    ret = OPENSSL_zalloc(sizeof(*ret));
    if (ret == NULL)
        goto err;

    ret->method = meth;
    ret->min_proto_version = 0;
    ret->max_proto_version = 0;
    ret->mode = SSL_MODE_AUTO_RETRY;
    ret->session_cache_mode = SSL_SESS_CACHE_SERVER;
    ret->session_cache_size = SSL_SESSION_CACHE_MAX_SIZE_DEFAULT;
    /* We take the system default. */
    ret->session_timeout = meth->get_timeout();
    ret->references = 1;
    ret->lock = CRYPTO_THREAD_lock_new();
    if (ret->lock == NULL) {
        SSLerr(SSL_F_SSL_CTX_NEW, ERR_R_MALLOC_FAILURE);
        OPENSSL_free(ret);
        return NULL;
    }
    ret->max_cert_list = SSL_MAX_CERT_LIST_DEFAULT;
    ret->verify_mode = SSL_VERIFY_NONE;
    if ((ret->cert = ssl_cert_new()) == NULL)
        goto err;

    ret->sessions = lh_SSL_SESSION_new(ssl_session_hash, ssl_session_cmp);
    if (ret->sessions == NULL)
        goto err;
    ret->cert_store = X509_STORE_new();
    if (ret->cert_store == NULL)
        goto err;
#ifndef OPENSSL_NO_CT
    ret->ctlog_store = CTLOG_STORE_new();
    if (ret->ctlog_store == NULL)
        goto err;
#endif

    if (!SSL_CTX_set_ciphersuites(ret, TLS_DEFAULT_CIPHERSUITES))
        goto err;

    if (!ssl_create_cipher_list(ret->method,
                                ret->tls13_ciphersuites,
                                &ret->cipher_list, &ret->cipher_list_by_id,
                                SSL_DEFAULT_CIPHER_LIST, ret->cert)
        || sk_SSL_CIPHER_num(ret->cipher_list) <= 0) {
        SSLerr(SSL_F_SSL_CTX_NEW, SSL_R_LIBRARY_HAS_NO_CIPHERS);
        goto err2;
    }

    ret->param = X509_VERIFY_PARAM_new();
    if (ret->param == NULL)
        goto err;

    if ((ret->md5 = EVP_get_digestbyname("ssl3-md5")) == NULL) {
        SSLerr(SSL_F_SSL_CTX_NEW, SSL_R_UNABLE_TO_LOAD_SSL3_MD5_ROUTINES);
        goto err2;
    }
    if ((ret->sha1 = EVP_get_digestbyname("ssl3-sha1")) == NULL) {
        SSLerr(SSL_F_SSL_CTX_NEW, SSL_R_UNABLE_TO_LOAD_SSL3_SHA1_ROUTINES);
        goto err2;
    }

    if ((ret->ca_names = sk_X509_NAME_new_null()) == NULL)
        goto err;

    if ((ret->client_ca_names = sk_X509_NAME_new_null()) == NULL)
        goto err;

    if (!CRYPTO_new_ex_data(CRYPTO_EX_INDEX_SSL_CTX, ret, &ret->ex_data))
        goto err;

    if ((ret->ext.secure = OPENSSL_secure_zalloc(sizeof(*ret->ext.secure))) == NULL)
        goto err;

    /* No compression for DTLS */
    if (!(meth->ssl3_enc->enc_flags & SSL_ENC_FLAG_DTLS))
        ret->comp_methods = SSL_COMP_get_compression_methods();

    ret->max_send_fragment = SSL3_RT_MAX_PLAIN_LENGTH;
    ret->split_send_fragment = SSL3_RT_MAX_PLAIN_LENGTH;

    /* Setup RFC5077 ticket keys */
    if ((RAND_bytes(ret->ext.tick_key_name,
                    sizeof(ret->ext.tick_key_name)) <= 0)
        || (RAND_priv_bytes(ret->ext.secure->tick_hmac_key,
                       sizeof(ret->ext.secure->tick_hmac_key)) <= 0)
        || (RAND_priv_bytes(ret->ext.secure->tick_aes_key,
                       sizeof(ret->ext.secure->tick_aes_key)) <= 0))
        ret->options |= SSL_OP_NO_TICKET;

    if (RAND_priv_bytes(ret->ext.cookie_hmac_key,
                   sizeof(ret->ext.cookie_hmac_key)) <= 0)
        goto err;

#ifndef OPENSSL_NO_SRP
    if (!SSL_CTX_SRP_CTX_init(ret))
        goto err;
#endif
#ifndef OPENSSL_NO_ENGINE
# ifdef OPENSSL_SSL_CLIENT_ENGINE_AUTO
#  define eng_strx(x)     #x
#  define eng_str(x)      eng_strx(x)
    /* Use specific client engine automatically... ignore errors */
    {
        ENGINE *eng;
        eng = ENGINE_by_id(eng_str(OPENSSL_SSL_CLIENT_ENGINE_AUTO));
        if (!eng) {
            ERR_clear_error();
            ENGINE_load_builtin_engines();
            eng = ENGINE_by_id(eng_str(OPENSSL_SSL_CLIENT_ENGINE_AUTO));
        }
        if (!eng || !SSL_CTX_set_client_cert_engine(ret, eng))
            ERR_clear_error();
    }
# endif
#endif
    /*
     * Default is to connect to non-RI servers. When RI is more widely
     * deployed might change this.
     */
    ret->options |= SSL_OP_LEGACY_SERVER_CONNECT;
    /*
     * Disable compression by default to prevent CRIME. Applications can
     * re-enable compression by configuring
     * SSL_CTX_clear_options(ctx, SSL_OP_NO_COMPRESSION);
     * or by using the SSL_CONF library. Similarly we also enable TLSv1.3
     * middlebox compatibility by default. This may be disabled by default in
     * a later OpenSSL version.
     */
    ret->options |= SSL_OP_NO_COMPRESSION | SSL_OP_ENABLE_MIDDLEBOX_COMPAT;

    ret->ext.status_type = TLSEXT_STATUSTYPE_nothing;

    /*
     * We cannot usefully set a default max_early_data here (which gets
     * propagated in SSL_new(), for the following reason: setting the
     * SSL field causes tls_construct_stoc_early_data() to tell the
     * client that early data will be accepted when constructing a TLS 1.3
     * session ticket, and the client will accordingly send us early data
     * when using that ticket (if the client has early data to send).
     * However, in order for the early data to actually be consumed by
     * the application, the application must also have calls to
     * SSL_read_early_data(); otherwise we'll just skip past the early data
     * and ignore it.  So, since the application must add calls to
     * SSL_read_early_data(), we also require them to add
     * calls to SSL_CTX_set_max_early_data() in order to use early data,
     * eliminating the bandwidth-wasting early data in the case described
     * above.
     */
    ret->max_early_data = 0;

    /*
     * Default recv_max_early_data is a fully loaded single record. Could be
     * split across multiple records in practice. We set this differently to
     * max_early_data so that, in the default case, we do not advertise any
     * support for early_data, but if a client were to send us some (e.g.
     * because of an old, stale ticket) then we will tolerate it and skip over
     * it.
     */
    ret->recv_max_early_data = SSL3_RT_MAX_PLAIN_LENGTH;

    /* By default we send two session tickets automatically in TLSv1.3 */
    ret->num_tickets = 2;

    ssl_ctx_system_config(ret);

    return ret;
 err:
    SSLerr(SSL_F_SSL_CTX_NEW, ERR_R_MALLOC_FAILURE);
 err2:
    SSL_CTX_free(ret);
    return NULL;
}

int SSL_CTX_up_ref(SSL_CTX *ctx)
{
    int i;

    if (CRYPTO_UP_REF(&ctx->references, &i, ctx->lock) <= 0)
        return 0;

    REF_PRINT_COUNT("SSL_CTX", ctx);
    REF_ASSERT_ISNT(i < 2);
    return ((i > 1) ? 1 : 0);
}

void SSL_CTX_free(SSL_CTX *a)
{
    int i;

    if (a == NULL)
        return;

    CRYPTO_DOWN_REF(&a->references, &i, a->lock);
    REF_PRINT_COUNT("SSL_CTX", a);
    if (i > 0)
        return;
    REF_ASSERT_ISNT(i < 0);

    X509_VERIFY_PARAM_free(a->param);
    dane_ctx_final(&a->dane);

    /*
     * Free internal session cache. However: the remove_cb() may reference
     * the ex_data of SSL_CTX, thus the ex_data store can only be removed
     * after the sessions were flushed.
     * As the ex_data handling routines might also touch the session cache,
     * the most secure solution seems to be: empty (flush) the cache, then
     * free ex_data, then finally free the cache.
     * (See ticket [openssl.org #212].)
     */
    if (a->sessions != NULL)
        SSL_CTX_flush_sessions(a, 0);

    CRYPTO_free_ex_data(CRYPTO_EX_INDEX_SSL_CTX, a, &a->ex_data);
    lh_SSL_SESSION_free(a->sessions);
    X509_STORE_free(a->cert_store);
#ifndef OPENSSL_NO_CT
    CTLOG_STORE_free(a->ctlog_store);
#endif
    sk_SSL_CIPHER_free(a->cipher_list);
    sk_SSL_CIPHER_free(a->cipher_list_by_id);
    sk_SSL_CIPHER_free(a->tls13_ciphersuites);
    ssl_cert_free(a->cert);
    sk_X509_NAME_pop_free(a->ca_names, X509_NAME_free);
    sk_X509_NAME_pop_free(a->client_ca_names, X509_NAME_free);
    sk_X509_pop_free(a->extra_certs, X509_free);
    a->comp_methods = NULL;
#ifndef OPENSSL_NO_SRTP
    sk_SRTP_PROTECTION_PROFILE_free(a->srtp_profiles);
#endif
#ifndef OPENSSL_NO_SRP
    SSL_CTX_SRP_CTX_free(a);
#endif
#ifndef OPENSSL_NO_ENGINE
    ENGINE_finish(a->client_cert_engine);
#endif

#ifndef OPENSSL_NO_EC
    OPENSSL_free(a->ext.ecpointformats);
    OPENSSL_free(a->ext.supportedgroups);
#endif
    OPENSSL_free(a->ext.alpn);
    OPENSSL_secure_free(a->ext.secure);

    CRYPTO_THREAD_lock_free(a->lock);

    OPENSSL_free(a);
}

void SSL_CTX_set_default_passwd_cb(SSL_CTX *ctx, pem_password_cb *cb)
{
    ctx->default_passwd_callback = cb;
}

void SSL_CTX_set_default_passwd_cb_userdata(SSL_CTX *ctx, void *u)
{
    ctx->default_passwd_callback_userdata = u;
}

pem_password_cb *SSL_CTX_get_default_passwd_cb(SSL_CTX *ctx)
{
    return ctx->default_passwd_callback;
}

void *SSL_CTX_get_default_passwd_cb_userdata(SSL_CTX *ctx)
{
    return ctx->default_passwd_callback_userdata;
}

void SSL_set_default_passwd_cb(SSL *s, pem_password_cb *cb)
{
    s->default_passwd_callback = cb;
}

void SSL_set_default_passwd_cb_userdata(SSL *s, void *u)
{
    s->default_passwd_callback_userdata = u;
}

pem_password_cb *SSL_get_default_passwd_cb(SSL *s)
{
    return s->default_passwd_callback;
}

void *SSL_get_default_passwd_cb_userdata(SSL *s)
{
    return s->default_passwd_callback_userdata;
}

void SSL_CTX_set_cert_verify_callback(SSL_CTX *ctx,
                                      int (*cb) (X509_STORE_CTX *, void *),
                                      void *arg)
{
    ctx->app_verify_callback = cb;
    ctx->app_verify_arg = arg;
}

void SSL_CTX_set_verify(SSL_CTX *ctx, int mode,
                        int (*cb) (int, X509_STORE_CTX *))
{
    ctx->verify_mode = mode;
    ctx->default_verify_callback = cb;
}

void SSL_CTX_set_verify_depth(SSL_CTX *ctx, int depth)
{
    X509_VERIFY_PARAM_set_depth(ctx->param, depth);
}

void SSL_CTX_set_cert_cb(SSL_CTX *c, int (*cb) (SSL *ssl, void *arg), void *arg)
{
    ssl_cert_set_cert_cb(c->cert, cb, arg);
}

void SSL_set_cert_cb(SSL *s, int (*cb) (SSL *ssl, void *arg), void *arg)
{
    ssl_cert_set_cert_cb(s->cert, cb, arg);
}

void ssl_set_masks(SSL *s)
{
    CERT *c = s->cert;
    uint32_t *pvalid = s->s3->tmp.valid_flags;
    int rsa_enc, rsa_sign, dh_tmp, dsa_sign;
    unsigned long mask_k, mask_a;
#ifndef OPENSSL_NO_EC
    int have_ecc_cert, ecdsa_ok;
#endif
    if (c == NULL)
        return;

#ifndef OPENSSL_NO_DH
    dh_tmp = (c->dh_tmp != NULL || c->dh_tmp_cb != NULL || c->dh_tmp_auto);
#else
    dh_tmp = 0;
#endif

    rsa_enc = pvalid[SSL_PKEY_RSA] & CERT_PKEY_VALID;
    rsa_sign = pvalid[SSL_PKEY_RSA] & CERT_PKEY_VALID;
    dsa_sign = pvalid[SSL_PKEY_DSA_SIGN] & CERT_PKEY_VALID;
#ifndef OPENSSL_NO_EC
    have_ecc_cert = pvalid[SSL_PKEY_ECC] & CERT_PKEY_VALID;
#endif
    mask_k = 0;
    mask_a = 0;

#ifdef CIPHER_DEBUG
    fprintf(stderr, "dht=%d re=%d rs=%d ds=%d\n",
            dh_tmp, rsa_enc, rsa_sign, dsa_sign);
#endif

#ifndef OPENSSL_NO_GOST
    if (ssl_has_cert(s, SSL_PKEY_GOST12_512)) {
        mask_k |= SSL_kGOST;
        mask_a |= SSL_aGOST12;
    }
    if (ssl_has_cert(s, SSL_PKEY_GOST12_256)) {
        mask_k |= SSL_kGOST;
        mask_a |= SSL_aGOST12;
    }
    if (ssl_has_cert(s, SSL_PKEY_GOST01)) {
        mask_k |= SSL_kGOST;
        mask_a |= SSL_aGOST01;
    }
#endif

    if (rsa_enc)
        mask_k |= SSL_kRSA;

    if (dh_tmp)
        mask_k |= SSL_kDHE;

    /*
     * If we only have an RSA-PSS certificate allow RSA authentication
     * if TLS 1.2 and peer supports it.
     */

    if (rsa_enc || rsa_sign || (ssl_has_cert(s, SSL_PKEY_RSA_PSS_SIGN)
                && pvalid[SSL_PKEY_RSA_PSS_SIGN] & CERT_PKEY_EXPLICIT_SIGN
                && TLS1_get_version(s) == TLS1_2_VERSION))
        mask_a |= SSL_aRSA;

    if (dsa_sign) {
        mask_a |= SSL_aDSS;
    }

    mask_a |= SSL_aNULL;

    /*
     * An ECC certificate may be usable for ECDH and/or ECDSA cipher suites
     * depending on the key usage extension.
     */
#ifndef OPENSSL_NO_EC
    if (have_ecc_cert) {
        uint32_t ex_kusage;
        ex_kusage = X509_get_key_usage(c->pkeys[SSL_PKEY_ECC].x509);
        ecdsa_ok = ex_kusage & X509v3_KU_DIGITAL_SIGNATURE;
        if (!(pvalid[SSL_PKEY_ECC] & CERT_PKEY_SIGN))
            ecdsa_ok = 0;
        if (ecdsa_ok)
            mask_a |= SSL_aECDSA;
    }
    /* Allow Ed25519 for TLS 1.2 if peer supports it */
    if (!(mask_a & SSL_aECDSA) && ssl_has_cert(s, SSL_PKEY_ED25519)
            && pvalid[SSL_PKEY_ED25519] & CERT_PKEY_EXPLICIT_SIGN
            && TLS1_get_version(s) == TLS1_2_VERSION)
            mask_a |= SSL_aECDSA;

    /* Allow Ed448 for TLS 1.2 if peer supports it */
    if (!(mask_a & SSL_aECDSA) && ssl_has_cert(s, SSL_PKEY_ED448)
            && pvalid[SSL_PKEY_ED448] & CERT_PKEY_EXPLICIT_SIGN
            && TLS1_get_version(s) == TLS1_2_VERSION)
            mask_a |= SSL_aECDSA;
#endif

#ifndef OPENSSL_NO_EC
    mask_k |= SSL_kECDHE;
#endif

#ifndef OPENSSL_NO_PSK
    mask_k |= SSL_kPSK;
    mask_a |= SSL_aPSK;
    if (mask_k & SSL_kRSA)
        mask_k |= SSL_kRSAPSK;
    if (mask_k & SSL_kDHE)
        mask_k |= SSL_kDHEPSK;
    if (mask_k & SSL_kECDHE)
        mask_k |= SSL_kECDHEPSK;
#endif

    s->s3->tmp.mask_k = mask_k;
    s->s3->tmp.mask_a = mask_a;
}

#ifndef OPENSSL_NO_EC

int ssl_check_srvr_ecc_cert_and_alg(X509 *x, SSL *s)
{
    if (s->s3->tmp.new_cipher->algorithm_auth & SSL_aECDSA) {
        /* key usage, if present, must allow signing */
        if (!(X509_get_key_usage(x) & X509v3_KU_DIGITAL_SIGNATURE)) {
            SSLerr(SSL_F_SSL_CHECK_SRVR_ECC_CERT_AND_ALG,
                   SSL_R_ECC_CERT_NOT_FOR_SIGNING);
            return 0;
        }
    }
    return 1;                   /* all checks are ok */
}

#endif

int ssl_get_server_cert_serverinfo(SSL *s, const unsigned char **serverinfo,
                                   size_t *serverinfo_length)
{
    CERT_PKEY *cpk = s->s3->tmp.cert;
    *serverinfo_length = 0;

    if (cpk == NULL || cpk->serverinfo == NULL)
        return 0;

    *serverinfo = cpk->serverinfo;
    *serverinfo_length = cpk->serverinfo_length;
    return 1;
}

void ssl_update_cache(SSL *s, int mode)
{
    int i;

    /*
     * If the session_id_length is 0, we are not supposed to cache it, and it
     * would be rather hard to do anyway :-)
     */
    if (s->session->session_id_length == 0)
        return;

    /*
     * If sid_ctx_length is 0 there is no specific application context
     * associated with this session, so when we try to resume it and
     * SSL_VERIFY_PEER is requested to verify the client identity, we have no
     * indication that this is actually a session for the proper application
     * context, and the *handshake* will fail, not just the resumption attempt.
     * Do not cache (on the server) these sessions that are not resumable
     * (clients can set SSL_VERIFY_PEER without needing a sid_ctx set).
     */
    if (s->server && s->session->sid_ctx_length == 0
            && (s->verify_mode & SSL_VERIFY_PEER) != 0)
        return;

    i = s->session_ctx->session_cache_mode;
    if ((i & mode) != 0
        && (!s->hit || SSL_IS_TLS13(s))) {
        /*
         * Add the session to the internal cache. In server side TLSv1.3 we
         * normally don't do this because by default it's a full stateless ticket
         * with only a dummy session id so there is no reason to cache it,
         * unless:
         * - we are doing early_data, in which case we cache so that we can
         *   detect replays
         * - the application has set a remove_session_cb so needs to know about
         *   session timeout events
         * - SSL_OP_NO_TICKET is set in which case it is a stateful ticket
         */
        if ((i & SSL_SESS_CACHE_NO_INTERNAL_STORE) == 0
                && (!SSL_IS_TLS13(s)
                    || !s->server
                    || (s->max_early_data > 0
                        && (s->options & SSL_OP_NO_ANTI_REPLAY) == 0)
                    || s->session_ctx->remove_session_cb != NULL
                    || (s->options & SSL_OP_NO_TICKET) != 0))
            SSL_CTX_add_session(s->session_ctx, s->session);

        /*
         * Add the session to the external cache. We do this even in server side
         * TLSv1.3 without early data because some applications just want to
         * know about the creation of a session and aren't doing a full cache.
         */
        if (s->session_ctx->new_session_cb != NULL) {
            SSL_SESSION_up_ref(s->session);
            if (!s->session_ctx->new_session_cb(s, s->session))
                SSL_SESSION_free(s->session);
        }
    }

    /* auto flush every 255 connections */
    if ((!(i & SSL_SESS_CACHE_NO_AUTO_CLEAR)) && ((i & mode) == mode)) {
        TSAN_QUALIFIER int *stat;
        if (mode & SSL_SESS_CACHE_CLIENT)
            stat = &s->session_ctx->stats.sess_connect_good;
        else
            stat = &s->session_ctx->stats.sess_accept_good;
        if ((tsan_load(stat) & 0xff) == 0xff)
            SSL_CTX_flush_sessions(s->session_ctx, (unsigned long)time(NULL));
    }
}

const SSL_METHOD *SSL_CTX_get_ssl_method(const SSL_CTX *ctx)
{
    return ctx->method;
}

const SSL_METHOD *SSL_get_ssl_method(const SSL *s)
{
    return s->method;
}

int SSL_set_ssl_method(SSL *s, const SSL_METHOD *meth)
{
    int ret = 1;

    if (s->method != meth) {
        const SSL_METHOD *sm = s->method;
        int (*hf) (SSL *) = s->handshake_func;

        if (sm->version == meth->version)
            s->method = meth;
        else {
            sm->ssl_free(s);
            s->method = meth;
            ret = s->method->ssl_new(s);
        }

        if (hf == sm->ssl_connect)
            s->handshake_func = meth->ssl_connect;
        else if (hf == sm->ssl_accept)
            s->handshake_func = meth->ssl_accept;
    }
    return ret;
}

int SSL_get_error(const SSL *s, int i)
{
    int reason;
    unsigned long l;
    BIO *bio;

    if (i > 0)
        return SSL_ERROR_NONE;

    /*
     * Make things return SSL_ERROR_SYSCALL when doing SSL_do_handshake etc,
     * where we do encode the error
     */
    if ((l = ERR_peek_error()) != 0) {
        if (ERR_GET_LIB(l) == ERR_LIB_SYS)
            return SSL_ERROR_SYSCALL;
        else
            return SSL_ERROR_SSL;
    }

    if (SSL_want_read(s)) {
        bio = SSL_get_rbio(s);
        if (BIO_should_read(bio))
            return SSL_ERROR_WANT_READ;
        else if (BIO_should_write(bio))
            /*
             * This one doesn't make too much sense ... We never try to write
             * to the rbio, and an application program where rbio and wbio
             * are separate couldn't even know what it should wait for.
             * However if we ever set s->rwstate incorrectly (so that we have
             * SSL_want_read(s) instead of SSL_want_write(s)) and rbio and
             * wbio *are* the same, this test works around that bug; so it
             * might be safer to keep it.
             */
            return SSL_ERROR_WANT_WRITE;
        else if (BIO_should_io_special(bio)) {
            reason = BIO_get_retry_reason(bio);
            if (reason == BIO_RR_CONNECT)
                return SSL_ERROR_WANT_CONNECT;
            else if (reason == BIO_RR_ACCEPT)
                return SSL_ERROR_WANT_ACCEPT;
            else
                return SSL_ERROR_SYSCALL; /* unknown */
        }
    }

    if (SSL_want_write(s)) {
        /* Access wbio directly - in order to use the buffered bio if present */
        bio = s->wbio;
        if (BIO_should_write(bio))
            return SSL_ERROR_WANT_WRITE;
        else if (BIO_should_read(bio))
            /*
             * See above (SSL_want_read(s) with BIO_should_write(bio))
             */
            return SSL_ERROR_WANT_READ;
        else if (BIO_should_io_special(bio)) {
            reason = BIO_get_retry_reason(bio);
            if (reason == BIO_RR_CONNECT)
                return SSL_ERROR_WANT_CONNECT;
            else if (reason == BIO_RR_ACCEPT)
                return SSL_ERROR_WANT_ACCEPT;
            else
                return SSL_ERROR_SYSCALL;
        }
    }
    if (SSL_want_x509_lookup(s))
        return SSL_ERROR_WANT_X509_LOOKUP;
    if (SSL_want_async(s))
        return SSL_ERROR_WANT_ASYNC;
    if (SSL_want_async_job(s))
        return SSL_ERROR_WANT_ASYNC_JOB;
    if (SSL_want_client_hello_cb(s))
        return SSL_ERROR_WANT_CLIENT_HELLO_CB;

    if ((s->shutdown & SSL_RECEIVED_SHUTDOWN) &&
        (s->s3->warn_alert == SSL_AD_CLOSE_NOTIFY))
        return SSL_ERROR_ZERO_RETURN;

    return SSL_ERROR_SYSCALL;
}

static int ssl_do_handshake_intern(void *vargs)
{
    struct ssl_async_args *args;
    SSL *s;

    args = (struct ssl_async_args *)vargs;
    s = args->s;

    return s->handshake_func(s);
}

int SSL_do_handshake(SSL *s)
{
    int ret = 1;

    if (s->handshake_func == NULL) {
        SSLerr(SSL_F_SSL_DO_HANDSHAKE, SSL_R_CONNECTION_TYPE_NOT_SET);
        return -1;
    }

    ossl_statem_check_finish_init(s, -1);

    s->method->ssl_renegotiate_check(s, 0);

    if (SSL_in_init(s) || SSL_in_before(s)) {
        if ((s->mode & SSL_MODE_ASYNC) && ASYNC_get_current_job() == NULL) {
            struct ssl_async_args args;

            args.s = s;

            ret = ssl_start_async_job(s, &args, ssl_do_handshake_intern);
        } else {
            ret = s->handshake_func(s);
        }
    }
    return ret;
}

void SSL_set_accept_state(SSL *s)
{
    s->server = 1;
    s->shutdown = 0;
    ossl_statem_clear(s);
    s->handshake_func = s->method->ssl_accept;
    clear_ciphers(s);
}

void SSL_set_connect_state(SSL *s)
{
    s->server = 0;
    s->shutdown = 0;
    ossl_statem_clear(s);
    s->handshake_func = s->method->ssl_connect;
    clear_ciphers(s);
}

int ssl_undefined_function(SSL *s)
{
    SSLerr(SSL_F_SSL_UNDEFINED_FUNCTION, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
    return 0;
}

int ssl_undefined_void_function(void)
{
    SSLerr(SSL_F_SSL_UNDEFINED_VOID_FUNCTION,
           ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
    return 0;
}

int ssl_undefined_const_function(const SSL *s)
{
    return 0;
}

const SSL_METHOD *ssl_bad_method(int ver)
{
    SSLerr(SSL_F_SSL_BAD_METHOD, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
    return NULL;
}

const char *ssl_protocol_to_string(int version)
{
    switch(version)
    {
    case TLS1_3_VERSION:
        return "TLSv1.3";

    case TLS1_2_VERSION:
        return "TLSv1.2";

    case TLS1_1_VERSION:
        return "TLSv1.1";

    case TLS1_VERSION:
        return "TLSv1";

    case SSL3_VERSION:
        return "SSLv3";

    case DTLS1_BAD_VER:
        return "DTLSv0.9";

    case DTLS1_VERSION:
        return "DTLSv1";

    case DTLS1_2_VERSION:
        return "DTLSv1.2";

    default:
        return "unknown";
    }
}

const char *SSL_get_version(const SSL *s)
{
    return ssl_protocol_to_string(s->version);
}

static int dup_ca_names(STACK_OF(X509_NAME) **dst, STACK_OF(X509_NAME) *src)
{
    STACK_OF(X509_NAME) *sk;
    X509_NAME *xn;
    int i;

    if (src == NULL) {
        *dst = NULL;
        return 1;
    }

    if ((sk = sk_X509_NAME_new_null()) == NULL)
        return 0;
    for (i = 0; i < sk_X509_NAME_num(src); i++) {
        xn = X509_NAME_dup(sk_X509_NAME_value(src, i));
        if (xn == NULL) {
            sk_X509_NAME_pop_free(sk, X509_NAME_free);
            return 0;
        }
        if (sk_X509_NAME_insert(sk, xn, i) == 0) {
            X509_NAME_free(xn);
            sk_X509_NAME_pop_free(sk, X509_NAME_free);
            return 0;
        }
    }
    *dst = sk;

    return 1;
}

SSL *SSL_dup(SSL *s)
{
    SSL *ret;
    int i;

    /* If we're not quiescent, just up_ref! */
    if (!SSL_in_init(s) || !SSL_in_before(s)) {
        CRYPTO_UP_REF(&s->references, &i, s->lock);
        return s;
    }

    /*
     * Otherwise, copy configuration state, and session if set.
     */
    if ((ret = SSL_new(SSL_get_SSL_CTX(s))) == NULL)
        return NULL;

    if (s->session != NULL) {
        /*
         * Arranges to share the same session via up_ref.  This "copies"
         * session-id, SSL_METHOD, sid_ctx, and 'cert'
         */
        if (!SSL_copy_session_id(ret, s))
            goto err;
    } else {
        /*
         * No session has been established yet, so we have to expect that
         * s->cert or ret->cert will be changed later -- they should not both
         * point to the same object, and thus we can't use
         * SSL_copy_session_id.
         */
        if (!SSL_set_ssl_method(ret, s->method))
            goto err;

        if (s->cert != NULL) {
            ssl_cert_free(ret->cert);
            ret->cert = ssl_cert_dup(s->cert);
            if (ret->cert == NULL)
                goto err;
        }

        if (!SSL_set_session_id_context(ret, s->sid_ctx,
                                        (int)s->sid_ctx_length))
            goto err;
    }

    if (!ssl_dane_dup(ret, s))
        goto err;
    ret->version = s->version;
    ret->options = s->options;
    ret->mode = s->mode;
    SSL_set_max_cert_list(ret, SSL_get_max_cert_list(s));
    SSL_set_read_ahead(ret, SSL_get_read_ahead(s));
    ret->msg_callback = s->msg_callback;
    ret->msg_callback_arg = s->msg_callback_arg;
    SSL_set_verify(ret, SSL_get_verify_mode(s), SSL_get_verify_callback(s));
    SSL_set_verify_depth(ret, SSL_get_verify_depth(s));
    ret->generate_session_id = s->generate_session_id;

    SSL_set_info_callback(ret, SSL_get_info_callback(s));

    /* copy app data, a little dangerous perhaps */
    if (!CRYPTO_dup_ex_data(CRYPTO_EX_INDEX_SSL, &ret->ex_data, &s->ex_data))
        goto err;

    /* setup rbio, and wbio */
    if (s->rbio != NULL) {
        if (!BIO_dup_state(s->rbio, (char *)&ret->rbio))
            goto err;
    }
    if (s->wbio != NULL) {
        if (s->wbio != s->rbio) {
            if (!BIO_dup_state(s->wbio, (char *)&ret->wbio))
                goto err;
        } else {
            BIO_up_ref(ret->rbio);
            ret->wbio = ret->rbio;
        }
    }

    ret->server = s->server;
    if (s->handshake_func) {
        if (s->server)
            SSL_set_accept_state(ret);
        else
            SSL_set_connect_state(ret);
    }
    ret->shutdown = s->shutdown;
    ret->hit = s->hit;

    ret->default_passwd_callback = s->default_passwd_callback;
    ret->default_passwd_callback_userdata = s->default_passwd_callback_userdata;

    X509_VERIFY_PARAM_inherit(ret->param, s->param);

    /* dup the cipher_list and cipher_list_by_id stacks */
    if (s->cipher_list != NULL) {
        if ((ret->cipher_list = sk_SSL_CIPHER_dup(s->cipher_list)) == NULL)
            goto err;
    }
    if (s->cipher_list_by_id != NULL)
        if ((ret->cipher_list_by_id = sk_SSL_CIPHER_dup(s->cipher_list_by_id))
            == NULL)
            goto err;

    /* Dup the client_CA list */
    if (!dup_ca_names(&ret->ca_names, s->ca_names)
            || !dup_ca_names(&ret->client_ca_names, s->client_ca_names))
        goto err;

    return ret;

 err:
    SSL_free(ret);
    return NULL;
}

void ssl_clear_cipher_ctx(SSL *s)
{
    if (s->enc_read_ctx != NULL) {
        EVP_CIPHER_CTX_free(s->enc_read_ctx);
        s->enc_read_ctx = NULL;
    }
    if (s->enc_write_ctx != NULL) {
        EVP_CIPHER_CTX_free(s->enc_write_ctx);
        s->enc_write_ctx = NULL;
    }
#ifndef OPENSSL_NO_COMP
    COMP_CTX_free(s->expand);
    s->expand = NULL;
    COMP_CTX_free(s->compress);
    s->compress = NULL;
#endif
}

X509 *SSL_get_certificate(const SSL *s)
{
    if (s->cert != NULL)
        return s->cert->key->x509;
    else
        return NULL;
}

EVP_PKEY *SSL_get_privatekey(const SSL *s)
{
    if (s->cert != NULL)
        return s->cert->key->privatekey;
    else
        return NULL;
}

X509 *SSL_CTX_get0_certificate(const SSL_CTX *ctx)
{
    if (ctx->cert != NULL)
        return ctx->cert->key->x509;
    else
        return NULL;
}

EVP_PKEY *SSL_CTX_get0_privatekey(const SSL_CTX *ctx)
{
    if (ctx->cert != NULL)
        return ctx->cert->key->privatekey;
    else
        return NULL;
}

const SSL_CIPHER *SSL_get_current_cipher(const SSL *s)
{
    if ((s->session != NULL) && (s->session->cipher != NULL))
        return s->session->cipher;
    return NULL;
}

const SSL_CIPHER *SSL_get_pending_cipher(const SSL *s)
{
    return s->s3->tmp.new_cipher;
}

const COMP_METHOD *SSL_get_current_compression(const SSL *s)
{
#ifndef OPENSSL_NO_COMP
    return s->compress ? COMP_CTX_get_method(s->compress) : NULL;
#else
    return NULL;
#endif
}

const COMP_METHOD *SSL_get_current_expansion(const SSL *s)
{
#ifndef OPENSSL_NO_COMP
    return s->expand ? COMP_CTX_get_method(s->expand) : NULL;
#else
    return NULL;
#endif
}

int ssl_init_wbio_buffer(SSL *s)
{
    BIO *bbio;

    if (s->bbio != NULL) {
        /* Already buffered. */
        return 1;
    }

    bbio = BIO_new(BIO_f_buffer());
    if (bbio == NULL || !BIO_set_read_buffer_size(bbio, 1)) {
        BIO_free(bbio);
        SSLerr(SSL_F_SSL_INIT_WBIO_BUFFER, ERR_R_BUF_LIB);
        return 0;
    }
    s->bbio = bbio;
    s->wbio = BIO_push(bbio, s->wbio);

    return 1;
}

int ssl_free_wbio_buffer(SSL *s)
{
    /* callers ensure s is never null */
    if (s->bbio == NULL)
        return 1;

    s->wbio = BIO_pop(s->wbio);
    BIO_free(s->bbio);
    s->bbio = NULL;

    return 1;
}

void SSL_CTX_set_quiet_shutdown(SSL_CTX *ctx, int mode)
{
    ctx->quiet_shutdown = mode;
}

int SSL_CTX_get_quiet_shutdown(const SSL_CTX *ctx)
{
    return ctx->quiet_shutdown;
}

void SSL_set_quiet_shutdown(SSL *s, int mode)
{
    s->quiet_shutdown = mode;
}

int SSL_get_quiet_shutdown(const SSL *s)
{
    return s->quiet_shutdown;
}

void SSL_set_shutdown(SSL *s, int mode)
{
    s->shutdown = mode;
}

int SSL_get_shutdown(const SSL *s)
{
    return s->shutdown;
}

int SSL_version(const SSL *s)
{
    return s->version;
}

int SSL_client_version(const SSL *s)
{
    return s->client_version;
}

SSL_CTX *SSL_get_SSL_CTX(const SSL *ssl)
{
    return ssl->ctx;
}

SSL_CTX *SSL_set_SSL_CTX(SSL *ssl, SSL_CTX *ctx)
{
    CERT *new_cert;
    if (ssl->ctx == ctx)
        return ssl->ctx;
    if (ctx == NULL)
        ctx = ssl->session_ctx;
    new_cert = ssl_cert_dup(ctx->cert);
    if (new_cert == NULL) {
        return NULL;
    }

    if (!custom_exts_copy_flags(&new_cert->custext, &ssl->cert->custext)) {
        ssl_cert_free(new_cert);
        return NULL;
    }

    ssl_cert_free(ssl->cert);
    ssl->cert = new_cert;

    /*
     * Program invariant: |sid_ctx| has fixed size (SSL_MAX_SID_CTX_LENGTH),
     * so setter APIs must prevent invalid lengths from entering the system.
     */
    if (!ossl_assert(ssl->sid_ctx_length <= sizeof(ssl->sid_ctx)))
        return NULL;

    /*
     * If the session ID context matches that of the parent SSL_CTX,
     * inherit it from the new SSL_CTX as well. If however the context does
     * not match (i.e., it was set per-ssl with SSL_set_session_id_context),
     * leave it unchanged.
     */
    if ((ssl->ctx != NULL) &&
        (ssl->sid_ctx_length == ssl->ctx->sid_ctx_length) &&
        (memcmp(ssl->sid_ctx, ssl->ctx->sid_ctx, ssl->sid_ctx_length) == 0)) {
        ssl->sid_ctx_length = ctx->sid_ctx_length;
        memcpy(&ssl->sid_ctx, &ctx->sid_ctx, sizeof(ssl->sid_ctx));
    }

    SSL_CTX_up_ref(ctx);
    SSL_CTX_free(ssl->ctx);     /* decrement reference count */
    ssl->ctx = ctx;

    return ssl->ctx;
}

int SSL_CTX_set_default_verify_paths(SSL_CTX *ctx)
{
    return X509_STORE_set_default_paths(ctx->cert_store);
}

int SSL_CTX_set_default_verify_dir(SSL_CTX *ctx)
{
    X509_LOOKUP *lookup;

    lookup = X509_STORE_add_lookup(ctx->cert_store, X509_LOOKUP_hash_dir());
    if (lookup == NULL)
        return 0;
    X509_LOOKUP_add_dir(lookup, NULL, X509_FILETYPE_DEFAULT);

    /* Clear any errors if the default directory does not exist */
    ERR_clear_error();

    return 1;
}

int SSL_CTX_set_default_verify_file(SSL_CTX *ctx)
{
    X509_LOOKUP *lookup;

    lookup = X509_STORE_add_lookup(ctx->cert_store, X509_LOOKUP_file());
    if (lookup == NULL)
        return 0;

    X509_LOOKUP_load_file(lookup, NULL, X509_FILETYPE_DEFAULT);

    /* Clear any errors if the default file does not exist */
    ERR_clear_error();

    return 1;
}

int SSL_CTX_load_verify_locations(SSL_CTX *ctx, const char *CAfile,
                                  const char *CApath)
{
    return X509_STORE_load_locations(ctx->cert_store, CAfile, CApath);
}

void SSL_set_info_callback(SSL *ssl,
                           void (*cb) (const SSL *ssl, int type, int val))
{
    ssl->info_callback = cb;
}

/*
 * One compiler (Diab DCC) doesn't like argument names in returned function
 * pointer.
 */
void (*SSL_get_info_callback(const SSL *ssl)) (const SSL * /* ssl */ ,
                                               int /* type */ ,
                                               int /* val */ ) {
    return ssl->info_callback;
}

void SSL_set_verify_result(SSL *ssl, long arg)
{
    ssl->verify_result = arg;
}

long SSL_get_verify_result(const SSL *ssl)
{
    return ssl->verify_result;
}

size_t SSL_get_client_random(const SSL *ssl, unsigned char *out, size_t outlen)
{
    if (outlen == 0)
        return sizeof(ssl->s3->client_random);
    if (outlen > sizeof(ssl->s3->client_random))
        outlen = sizeof(ssl->s3->client_random);
    memcpy(out, ssl->s3->client_random, outlen);
    return outlen;
}

size_t SSL_get_server_random(const SSL *ssl, unsigned char *out, size_t outlen)
{
    if (outlen == 0)
        return sizeof(ssl->s3->server_random);
    if (outlen > sizeof(ssl->s3->server_random))
        outlen = sizeof(ssl->s3->server_random);
    memcpy(out, ssl->s3->server_random, outlen);
    return outlen;
}

size_t SSL_SESSION_get_master_key(const SSL_SESSION *session,
                                  unsigned char *out, size_t outlen)
{
    if (outlen == 0)
        return session->master_key_length;
    if (outlen > session->master_key_length)
        outlen = session->master_key_length;
    memcpy(out, session->master_key, outlen);
    return outlen;
}

int SSL_SESSION_set1_master_key(SSL_SESSION *sess, const unsigned char *in,
                                size_t len)
{
    if (len > sizeof(sess->master_key))
        return 0;

    memcpy(sess->master_key, in, len);
    sess->master_key_length = len;
    return 1;
}


int SSL_set_ex_data(SSL *s, int idx, void *arg)
{
    return CRYPTO_set_ex_data(&s->ex_data, idx, arg);
}

void *SSL_get_ex_data(const SSL *s, int idx)
{
    return CRYPTO_get_ex_data(&s->ex_data, idx);
}

int SSL_CTX_set_ex_data(SSL_CTX *s, int idx, void *arg)
{
    return CRYPTO_set_ex_data(&s->ex_data, idx, arg);
}

void *SSL_CTX_get_ex_data(const SSL_CTX *s, int idx)
{
    return CRYPTO_get_ex_data(&s->ex_data, idx);
}

X509_STORE *SSL_CTX_get_cert_store(const SSL_CTX *ctx)
{
    return ctx->cert_store;
}

void SSL_CTX_set_cert_store(SSL_CTX *ctx, X509_STORE *store)
{
    X509_STORE_free(ctx->cert_store);
    ctx->cert_store = store;
}

void SSL_CTX_set1_cert_store(SSL_CTX *ctx, X509_STORE *store)
{
    if (store != NULL)
        X509_STORE_up_ref(store);
    SSL_CTX_set_cert_store(ctx, store);
}

int SSL_want(const SSL *s)
{
    return s->rwstate;
}

/**
 * \brief Set the callback for generating temporary DH keys.
 * \param ctx the SSL context.
 * \param dh the callback
 */

#ifndef OPENSSL_NO_DH
void SSL_CTX_set_tmp_dh_callback(SSL_CTX *ctx,
                                 DH *(*dh) (SSL *ssl, int is_export,
                                            int keylength))
{
    SSL_CTX_callback_ctrl(ctx, SSL_CTRL_SET_TMP_DH_CB, (void (*)(void))dh);
}

void SSL_set_tmp_dh_callback(SSL *ssl, DH *(*dh) (SSL *ssl, int is_export,
                                                  int keylength))
{
    SSL_callback_ctrl(ssl, SSL_CTRL_SET_TMP_DH_CB, (void (*)(void))dh);
}
#endif

#ifndef OPENSSL_NO_PSK
int SSL_CTX_use_psk_identity_hint(SSL_CTX *ctx, const char *identity_hint)
{
    if (identity_hint != NULL && strlen(identity_hint) > PSK_MAX_IDENTITY_LEN) {
        SSLerr(SSL_F_SSL_CTX_USE_PSK_IDENTITY_HINT, SSL_R_DATA_LENGTH_TOO_LONG);
        return 0;
    }
    OPENSSL_free(ctx->cert->psk_identity_hint);
    if (identity_hint != NULL) {
        ctx->cert->psk_identity_hint = OPENSSL_strdup(identity_hint);
        if (ctx->cert->psk_identity_hint == NULL)
            return 0;
    } else
        ctx->cert->psk_identity_hint = NULL;
    return 1;
}

int SSL_use_psk_identity_hint(SSL *s, const char *identity_hint)
{
    if (s == NULL)
        return 0;

    if (identity_hint != NULL && strlen(identity_hint) > PSK_MAX_IDENTITY_LEN) {
        SSLerr(SSL_F_SSL_USE_PSK_IDENTITY_HINT, SSL_R_DATA_LENGTH_TOO_LONG);
        return 0;
    }
    OPENSSL_free(s->cert->psk_identity_hint);
    if (identity_hint != NULL) {
        s->cert->psk_identity_hint = OPENSSL_strdup(identity_hint);
        if (s->cert->psk_identity_hint == NULL)
            return 0;
    } else
        s->cert->psk_identity_hint = NULL;
    return 1;
}

const char *SSL_get_psk_identity_hint(const SSL *s)
{
    if (s == NULL || s->session == NULL)
        return NULL;
    return s->session->psk_identity_hint;
}

const char *SSL_get_psk_identity(const SSL *s)
{
    if (s == NULL || s->session == NULL)
        return NULL;
    return s->session->psk_identity;
}

void SSL_set_psk_client_callback(SSL *s, SSL_psk_client_cb_func cb)
{
    s->psk_client_callback = cb;
}

void SSL_CTX_set_psk_client_callback(SSL_CTX *ctx, SSL_psk_client_cb_func cb)
{
    ctx->psk_client_callback = cb;
}

void SSL_set_psk_server_callback(SSL *s, SSL_psk_server_cb_func cb)
{
    s->psk_server_callback = cb;
}

void SSL_CTX_set_psk_server_callback(SSL_CTX *ctx, SSL_psk_server_cb_func cb)
{
    ctx->psk_server_callback = cb;
}
#endif

void SSL_set_psk_find_session_callback(SSL *s, SSL_psk_find_session_cb_func cb)
{
    s->psk_find_session_cb = cb;
}

void SSL_CTX_set_psk_find_session_callback(SSL_CTX *ctx,
                                           SSL_psk_find_session_cb_func cb)
{
    ctx->psk_find_session_cb = cb;
}

void SSL_set_psk_use_session_callback(SSL *s, SSL_psk_use_session_cb_func cb)
{
    s->psk_use_session_cb = cb;
}

void SSL_CTX_set_psk_use_session_callback(SSL_CTX *ctx,
                                           SSL_psk_use_session_cb_func cb)
{
    ctx->psk_use_session_cb = cb;
}

void SSL_CTX_set_msg_callback(SSL_CTX *ctx,
                              void (*cb) (int write_p, int version,
                                          int content_type, const void *buf,
                                          size_t len, SSL *ssl, void *arg))
{
    SSL_CTX_callback_ctrl(ctx, SSL_CTRL_SET_MSG_CALLBACK, (void (*)(void))cb);
}

void SSL_set_msg_callback(SSL *ssl,
                          void (*cb) (int write_p, int version,
                                      int content_type, const void *buf,
                                      size_t len, SSL *ssl, void *arg))
{
    SSL_callback_ctrl(ssl, SSL_CTRL_SET_MSG_CALLBACK, (void (*)(void))cb);
}

void SSL_CTX_set_not_resumable_session_callback(SSL_CTX *ctx,
                                                int (*cb) (SSL *ssl,
                                                           int
                                                           is_forward_secure))
{
    SSL_CTX_callback_ctrl(ctx, SSL_CTRL_SET_NOT_RESUMABLE_SESS_CB,
                          (void (*)(void))cb);
}

void SSL_set_not_resumable_session_callback(SSL *ssl,
                                            int (*cb) (SSL *ssl,
                                                       int is_forward_secure))
{
    SSL_callback_ctrl(ssl, SSL_CTRL_SET_NOT_RESUMABLE_SESS_CB,
                      (void (*)(void))cb);
}

void SSL_CTX_set_record_padding_callback(SSL_CTX *ctx,
                                         size_t (*cb) (SSL *ssl, int type,
                                                       size_t len, void *arg))
{
    ctx->record_padding_cb = cb;
}

void SSL_CTX_set_record_padding_callback_arg(SSL_CTX *ctx, void *arg)
{
    ctx->record_padding_arg = arg;
}

void *SSL_CTX_get_record_padding_callback_arg(const SSL_CTX *ctx)
{
    return ctx->record_padding_arg;
}

int SSL_CTX_set_block_padding(SSL_CTX *ctx, size_t block_size)
{
    /* block size of 0 or 1 is basically no padding */
    if (block_size == 1)
        ctx->block_padding = 0;
    else if (block_size <= SSL3_RT_MAX_PLAIN_LENGTH)
        ctx->block_padding = block_size;
    else
        return 0;
    return 1;
}

void SSL_set_record_padding_callback(SSL *ssl,
                                     size_t (*cb) (SSL *ssl, int type,
                                                   size_t len, void *arg))
{
    ssl->record_padding_cb = cb;
}

void SSL_set_record_padding_callback_arg(SSL *ssl, void *arg)
{
    ssl->record_padding_arg = arg;
}

void *SSL_get_record_padding_callback_arg(const SSL *ssl)
{
    return ssl->record_padding_arg;
}

int SSL_set_block_padding(SSL *ssl, size_t block_size)
{
    /* block size of 0 or 1 is basically no padding */
    if (block_size == 1)
        ssl->block_padding = 0;
    else if (block_size <= SSL3_RT_MAX_PLAIN_LENGTH)
        ssl->block_padding = block_size;
    else
        return 0;
    return 1;
}

int SSL_set_num_tickets(SSL *s, size_t num_tickets)
{
    s->num_tickets = num_tickets;

    return 1;
}

size_t SSL_get_num_tickets(const SSL *s)
{
    return s->num_tickets;
}

int SSL_CTX_set_num_tickets(SSL_CTX *ctx, size_t num_tickets)
{
    ctx->num_tickets = num_tickets;

    return 1;
}

size_t SSL_CTX_get_num_tickets(const SSL_CTX *ctx)
{
    return ctx->num_tickets;
}

/*
 * Allocates new EVP_MD_CTX and sets pointer to it into given pointer
 * variable, freeing EVP_MD_CTX previously stored in that variable, if any.
 * If EVP_MD pointer is passed, initializes ctx with this |md|.
 * Returns the newly allocated ctx;
 */

EVP_MD_CTX *ssl_replace_hash(EVP_MD_CTX **hash, const EVP_MD *md)
{
    ssl_clear_hash_ctx(hash);
    *hash = EVP_MD_CTX_new();
    if (*hash == NULL || (md && EVP_DigestInit_ex(*hash, md, NULL) <= 0)) {
        EVP_MD_CTX_free(*hash);
        *hash = NULL;
        return NULL;
    }
    return *hash;
}

void ssl_clear_hash_ctx(EVP_MD_CTX **hash)
{

    EVP_MD_CTX_free(*hash);
    *hash = NULL;
}

/* Retrieve handshake hashes */
int ssl_handshake_hash(SSL *s, unsigned char *out, size_t outlen,
                       size_t *hashlen)
{
    EVP_MD_CTX *ctx = NULL;
    EVP_MD_CTX *hdgst = s->s3->handshake_dgst;
    int hashleni = EVP_MD_CTX_size(hdgst);
    int ret = 0;

    if (hashleni < 0 || (size_t)hashleni > outlen) {
        SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_SSL_HANDSHAKE_HASH,
                 ERR_R_INTERNAL_ERROR);
        goto err;
    }

    ctx = EVP_MD_CTX_new();
    if (ctx == NULL)
        goto err;

    if (!EVP_MD_CTX_copy_ex(ctx, hdgst)
        || EVP_DigestFinal_ex(ctx, out, NULL) <= 0) {
        SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_SSL_HANDSHAKE_HASH,
                 ERR_R_INTERNAL_ERROR);
        goto err;
    }

    *hashlen = hashleni;

    ret = 1;
 err:
    EVP_MD_CTX_free(ctx);
    return ret;
}

int SSL_session_reused(SSL *s)
{
    return s->hit;
}

int SSL_is_server(const SSL *s)
{
    return s->server;
}

#if OPENSSL_API_COMPAT < 0x10100000L
void SSL_set_debug(SSL *s, int debug)
{
    /* Old function was do-nothing anyway... */
    (void)s;
    (void)debug;
}
#endif

void SSL_set_security_level(SSL *s, int level)
{
    s->cert->sec_level = level;
}

int SSL_get_security_level(const SSL *s)
{
    return s->cert->sec_level;
}

void SSL_set_security_callback(SSL *s,
                               int (*cb) (const SSL *s, const SSL_CTX *ctx,
                                          int op, int bits, int nid,
                                          void *other, void *ex))
{
    s->cert->sec_cb = cb;
}

int (*SSL_get_security_callback(const SSL *s)) (const SSL *s,
                                                const SSL_CTX *ctx, int op,
                                                int bits, int nid, void *other,
                                                void *ex) {
    return s->cert->sec_cb;
}

void SSL_set0_security_ex_data(SSL *s, void *ex)
{
    s->cert->sec_ex = ex;
}

void *SSL_get0_security_ex_data(const SSL *s)
{
    return s->cert->sec_ex;
}

void SSL_CTX_set_security_level(SSL_CTX *ctx, int level)
{
    ctx->cert->sec_level = level;
}

int SSL_CTX_get_security_level(const SSL_CTX *ctx)
{
    return ctx->cert->sec_level;
}

void SSL_CTX_set_security_callback(SSL_CTX *ctx,
                                   int (*cb) (const SSL *s, const SSL_CTX *ctx,
                                              int op, int bits, int nid,
                                              void *other, void *ex))
{
    ctx->cert->sec_cb = cb;
}

int (*SSL_CTX_get_security_callback(const SSL_CTX *ctx)) (const SSL *s,
                                                          const SSL_CTX *ctx,
                                                          int op, int bits,
                                                          int nid,
                                                          void *other,
                                                          void *ex) {
    return ctx->cert->sec_cb;
}

void SSL_CTX_set0_security_ex_data(SSL_CTX *ctx, void *ex)
{
    ctx->cert->sec_ex = ex;
}

void *SSL_CTX_get0_security_ex_data(const SSL_CTX *ctx)
{
    return ctx->cert->sec_ex;
}

/*
 * Get/Set/Clear options in SSL_CTX or SSL, formerly macros, now functions that
 * can return unsigned long, instead of the generic long return value from the
 * control interface.
 */
unsigned long SSL_CTX_get_options(const SSL_CTX *ctx)
{
    return ctx->options;
}

unsigned long SSL_get_options(const SSL *s)
{
    return s->options;
}

unsigned long SSL_CTX_set_options(SSL_CTX *ctx, unsigned long op)
{
    return ctx->options |= op;
}

unsigned long SSL_set_options(SSL *s, unsigned long op)
{
    return s->options |= op;
}

unsigned long SSL_CTX_clear_options(SSL_CTX *ctx, unsigned long op)
{
    return ctx->options &= ~op;
}

unsigned long SSL_clear_options(SSL *s, unsigned long op)
{
    return s->options &= ~op;
}

STACK_OF(X509) *SSL_get0_verified_chain(const SSL *s)
{
    return s->verified_chain;
}

IMPLEMENT_OBJ_BSEARCH_GLOBAL_CMP_FN(SSL_CIPHER, SSL_CIPHER, ssl_cipher_id);

#ifndef OPENSSL_NO_CT

/*
 * Moves SCTs from the |src| stack to the |dst| stack.
 * The source of each SCT will be set to |origin|.
 * If |dst| points to a NULL pointer, a new stack will be created and owned by
 * the caller.
 * Returns the number of SCTs moved, or a negative integer if an error occurs.
 */
static int ct_move_scts(STACK_OF(SCT) **dst, STACK_OF(SCT) *src,
                        sct_source_t origin)
{
    int scts_moved = 0;
    SCT *sct = NULL;

    if (*dst == NULL) {
        *dst = sk_SCT_new_null();
        if (*dst == NULL) {
            SSLerr(SSL_F_CT_MOVE_SCTS, ERR_R_MALLOC_FAILURE);
            goto err;
        }
    }

    while ((sct = sk_SCT_pop(src)) != NULL) {
        if (SCT_set_source(sct, origin) != 1)
            goto err;

        if (sk_SCT_push(*dst, sct) <= 0)
            goto err;
        scts_moved += 1;
    }

    return scts_moved;
 err:
    if (sct != NULL)
        sk_SCT_push(src, sct);  /* Put the SCT back */
    return -1;
}

/*
 * Look for data collected during ServerHello and parse if found.
 * Returns the number of SCTs extracted.
 */
static int ct_extract_tls_extension_scts(SSL *s)
{
    int scts_extracted = 0;

    if (s->ext.scts != NULL) {
        const unsigned char *p = s->ext.scts;
        STACK_OF(SCT) *scts = o2i_SCT_LIST(NULL, &p, s->ext.scts_len);

        scts_extracted = ct_move_scts(&s->scts, scts, SCT_SOURCE_TLS_EXTENSION);

        SCT_LIST_free(scts);
    }

    return scts_extracted;
}

/*
 * Checks for an OCSP response and then attempts to extract any SCTs found if it
 * contains an SCT X509 extension. They will be stored in |s->scts|.
 * Returns:
 * - The number of SCTs extracted, assuming an OCSP response exists.
 * - 0 if no OCSP response exists or it contains no SCTs.
 * - A negative integer if an error occurs.
 */
static int ct_extract_ocsp_response_scts(SSL *s)
{
# ifndef OPENSSL_NO_OCSP
    int scts_extracted = 0;
    const unsigned char *p;
    OCSP_BASICRESP *br = NULL;
    OCSP_RESPONSE *rsp = NULL;
    STACK_OF(SCT) *scts = NULL;
    int i;

    if (s->ext.ocsp.resp == NULL || s->ext.ocsp.resp_len == 0)
        goto err;

    p = s->ext.ocsp.resp;
    rsp = d2i_OCSP_RESPONSE(NULL, &p, (int)s->ext.ocsp.resp_len);
    if (rsp == NULL)
        goto err;

    br = OCSP_response_get1_basic(rsp);
    if (br == NULL)
        goto err;

    for (i = 0; i < OCSP_resp_count(br); ++i) {
        OCSP_SINGLERESP *single = OCSP_resp_get0(br, i);

        if (single == NULL)
            continue;

        scts =
            OCSP_SINGLERESP_get1_ext_d2i(single, NID_ct_cert_scts, NULL, NULL);
        scts_extracted =
            ct_move_scts(&s->scts, scts, SCT_SOURCE_OCSP_STAPLED_RESPONSE);
        if (scts_extracted < 0)
            goto err;
    }
 err:
    SCT_LIST_free(scts);
    OCSP_BASICRESP_free(br);
    OCSP_RESPONSE_free(rsp);
    return scts_extracted;
# else
    /* Behave as if no OCSP response exists */
    return 0;
# endif
}

/*
 * Attempts to extract SCTs from the peer certificate.
 * Return the number of SCTs extracted, or a negative integer if an error
 * occurs.
 */
static int ct_extract_x509v3_extension_scts(SSL *s)
{
    int scts_extracted = 0;
    X509 *cert = s->session != NULL ? s->session->peer : NULL;

    if (cert != NULL) {
        STACK_OF(SCT) *scts =
            X509_get_ext_d2i(cert, NID_ct_precert_scts, NULL, NULL);

        scts_extracted =
            ct_move_scts(&s->scts, scts, SCT_SOURCE_X509V3_EXTENSION);

        SCT_LIST_free(scts);
    }

    return scts_extracted;
}

/*
 * Attempts to find all received SCTs by checking TLS extensions, the OCSP
 * response (if it exists) and X509v3 extensions in the certificate.
 * Returns NULL if an error occurs.
 */
const STACK_OF(SCT) *SSL_get0_peer_scts(SSL *s)
{
    if (!s->scts_parsed) {
        if (ct_extract_tls_extension_scts(s) < 0 ||
            ct_extract_ocsp_response_scts(s) < 0 ||
            ct_extract_x509v3_extension_scts(s) < 0)
            goto err;

        s->scts_parsed = 1;
    }
    return s->scts;
 err:
    return NULL;
}

static int ct_permissive(const CT_POLICY_EVAL_CTX * ctx,
                         const STACK_OF(SCT) *scts, void *unused_arg)
{
    return 1;
}

static int ct_strict(const CT_POLICY_EVAL_CTX * ctx,
                     const STACK_OF(SCT) *scts, void *unused_arg)
{
    int count = scts != NULL ? sk_SCT_num(scts) : 0;
    int i;

    for (i = 0; i < count; ++i) {
        SCT *sct = sk_SCT_value(scts, i);
        int status = SCT_get_validation_status(sct);

        if (status == SCT_VALIDATION_STATUS_VALID)
            return 1;
    }
    SSLerr(SSL_F_CT_STRICT, SSL_R_NO_VALID_SCTS);
    return 0;
}

int SSL_set_ct_validation_callback(SSL *s, ssl_ct_validation_cb callback,
                                   void *arg)
{
    /*
     * Since code exists that uses the custom extension handler for CT, look
     * for this and throw an error if they have already registered to use CT.
     */
    if (callback != NULL && SSL_CTX_has_client_custom_ext(s->ctx,
                                                          TLSEXT_TYPE_signed_certificate_timestamp))
    {
        SSLerr(SSL_F_SSL_SET_CT_VALIDATION_CALLBACK,
               SSL_R_CUSTOM_EXT_HANDLER_ALREADY_INSTALLED);
        return 0;
    }

    if (callback != NULL) {
        /*
         * If we are validating CT, then we MUST accept SCTs served via OCSP
         */
        if (!SSL_set_tlsext_status_type(s, TLSEXT_STATUSTYPE_ocsp))
            return 0;
    }

    s->ct_validation_callback = callback;
    s->ct_validation_callback_arg = arg;

    return 1;
}

int SSL_CTX_set_ct_validation_callback(SSL_CTX *ctx,
                                       ssl_ct_validation_cb callback, void *arg)
{
    /*
     * Since code exists that uses the custom extension handler for CT, look for
     * this and throw an error if they have already registered to use CT.
     */
    if (callback != NULL && SSL_CTX_has_client_custom_ext(ctx,
                                                          TLSEXT_TYPE_signed_certificate_timestamp))
    {
        SSLerr(SSL_F_SSL_CTX_SET_CT_VALIDATION_CALLBACK,
               SSL_R_CUSTOM_EXT_HANDLER_ALREADY_INSTALLED);
        return 0;
    }

    ctx->ct_validation_callback = callback;
    ctx->ct_validation_callback_arg = arg;
    return 1;
}

int SSL_ct_is_enabled(const SSL *s)
{
    return s->ct_validation_callback != NULL;
}

int SSL_CTX_ct_is_enabled(const SSL_CTX *ctx)
{
    return ctx->ct_validation_callback != NULL;
}

int ssl_validate_ct(SSL *s)
{
    int ret = 0;
    X509 *cert = s->session != NULL ? s->session->peer : NULL;
    X509 *issuer;
    SSL_DANE *dane = &s->dane;
    CT_POLICY_EVAL_CTX *ctx = NULL;
    const STACK_OF(SCT) *scts;

    /*
     * If no callback is set, the peer is anonymous, or its chain is invalid,
     * skip SCT validation - just return success.  Applications that continue
     * handshakes without certificates, with unverified chains, or pinned leaf
     * certificates are outside the scope of the WebPKI and CT.
     *
     * The above exclusions notwithstanding the vast majority of peers will
     * have rather ordinary certificate chains validated by typical
     * applications that perform certificate verification and therefore will
     * process SCTs when enabled.
     */
    if (s->ct_validation_callback == NULL || cert == NULL ||
        s->verify_result != X509_V_OK ||
        s->verified_chain == NULL || sk_X509_num(s->verified_chain) <= 1)
        return 1;

    /*
     * CT not applicable for chains validated via DANE-TA(2) or DANE-EE(3)
     * trust-anchors.  See https://tools.ietf.org/html/rfc7671#section-4.2
     */
    if (DANETLS_ENABLED(dane) && dane->mtlsa != NULL) {
        switch (dane->mtlsa->usage) {
        case DANETLS_USAGE_DANE_TA:
        case DANETLS_USAGE_DANE_EE:
            return 1;
        }
    }

    ctx = CT_POLICY_EVAL_CTX_new();
    if (ctx == NULL) {
        SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_SSL_VALIDATE_CT,
                 ERR_R_MALLOC_FAILURE);
        goto end;
    }

    issuer = sk_X509_value(s->verified_chain, 1);
    CT_POLICY_EVAL_CTX_set1_cert(ctx, cert);
    CT_POLICY_EVAL_CTX_set1_issuer(ctx, issuer);
    CT_POLICY_EVAL_CTX_set_shared_CTLOG_STORE(ctx, s->ctx->ctlog_store);
    CT_POLICY_EVAL_CTX_set_time(
            ctx, (uint64_t)SSL_SESSION_get_time(SSL_get0_session(s)) * 1000);

    scts = SSL_get0_peer_scts(s);

    /*
     * This function returns success (> 0) only when all the SCTs are valid, 0
     * when some are invalid, and < 0 on various internal errors (out of
     * memory, etc.).  Having some, or even all, invalid SCTs is not sufficient
     * reason to abort the handshake, that decision is up to the callback.
     * Therefore, we error out only in the unexpected case that the return
     * value is negative.
     *
     * XXX: One might well argue that the return value of this function is an
     * unfortunate design choice.  Its job is only to determine the validation
     * status of each of the provided SCTs.  So long as it correctly separates
     * the wheat from the chaff it should return success.  Failure in this case
     * ought to correspond to an inability to carry out its duties.
     */
    if (SCT_LIST_validate(scts, ctx) < 0) {
        SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE, SSL_F_SSL_VALIDATE_CT,
                 SSL_R_SCT_VERIFICATION_FAILED);
        goto end;
    }

    ret = s->ct_validation_callback(ctx, scts, s->ct_validation_callback_arg);
    if (ret < 0)
        ret = 0;                /* This function returns 0 on failure */
    if (!ret)
        SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE, SSL_F_SSL_VALIDATE_CT,
                 SSL_R_CALLBACK_FAILED);

 end:
    CT_POLICY_EVAL_CTX_free(ctx);
    /*
     * With SSL_VERIFY_NONE the session may be cached and re-used despite a
     * failure return code here.  Also the application may wish the complete
     * the handshake, and then disconnect cleanly at a higher layer, after
     * checking the verification status of the completed connection.
     *
     * We therefore force a certificate verification failure which will be
     * visible via SSL_get_verify_result() and cached as part of any resumed
     * session.
     *
     * Note: the permissive callback is for information gathering only, always
     * returns success, and does not affect verification status.  Only the
     * strict callback or a custom application-specified callback can trigger
     * connection failure or record a verification error.
     */
    if (ret <= 0)
        s->verify_result = X509_V_ERR_NO_VALID_SCTS;
    return ret;
}

int SSL_CTX_enable_ct(SSL_CTX *ctx, int validation_mode)
{
    switch (validation_mode) {
    default:
        SSLerr(SSL_F_SSL_CTX_ENABLE_CT, SSL_R_INVALID_CT_VALIDATION_TYPE);
        return 0;
    case SSL_CT_VALIDATION_PERMISSIVE:
        return SSL_CTX_set_ct_validation_callback(ctx, ct_permissive, NULL);
    case SSL_CT_VALIDATION_STRICT:
        return SSL_CTX_set_ct_validation_callback(ctx, ct_strict, NULL);
    }
}

int SSL_enable_ct(SSL *s, int validation_mode)
{
    switch (validation_mode) {
    default:
        SSLerr(SSL_F_SSL_ENABLE_CT, SSL_R_INVALID_CT_VALIDATION_TYPE);
        return 0;
    case SSL_CT_VALIDATION_PERMISSIVE:
        return SSL_set_ct_validation_callback(s, ct_permissive, NULL);
    case SSL_CT_VALIDATION_STRICT:
        return SSL_set_ct_validation_callback(s, ct_strict, NULL);
    }
}

int SSL_CTX_set_default_ctlog_list_file(SSL_CTX *ctx)
{
    return CTLOG_STORE_load_default_file(ctx->ctlog_store);
}

int SSL_CTX_set_ctlog_list_file(SSL_CTX *ctx, const char *path)
{
    return CTLOG_STORE_load_file(ctx->ctlog_store, path);
}

void SSL_CTX_set0_ctlog_store(SSL_CTX *ctx, CTLOG_STORE * logs)
{
    CTLOG_STORE_free(ctx->ctlog_store);
    ctx->ctlog_store = logs;
}

const CTLOG_STORE *SSL_CTX_get0_ctlog_store(const SSL_CTX *ctx)
{
    return ctx->ctlog_store;
}

#endif  /* OPENSSL_NO_CT */

void SSL_CTX_set_client_hello_cb(SSL_CTX *c, SSL_client_hello_cb_fn cb,
                                 void *arg)
{
    c->client_hello_cb = cb;
    c->client_hello_cb_arg = arg;
}

int SSL_client_hello_isv2(SSL *s)
{
    if (s->clienthello == NULL)
        return 0;
    return s->clienthello->isv2;
}

unsigned int SSL_client_hello_get0_legacy_version(SSL *s)
{
    if (s->clienthello == NULL)
        return 0;
    return s->clienthello->legacy_version;
}

size_t SSL_client_hello_get0_random(SSL *s, const unsigned char **out)
{
    if (s->clienthello == NULL)
        return 0;
    if (out != NULL)
        *out = s->clienthello->random;
    return SSL3_RANDOM_SIZE;
}

size_t SSL_client_hello_get0_session_id(SSL *s, const unsigned char **out)
{
    if (s->clienthello == NULL)
        return 0;
    if (out != NULL)
        *out = s->clienthello->session_id;
    return s->clienthello->session_id_len;
}

size_t SSL_client_hello_get0_ciphers(SSL *s, const unsigned char **out)
{
    if (s->clienthello == NULL)
        return 0;
    if (out != NULL)
        *out = PACKET_data(&s->clienthello->ciphersuites);
    return PACKET_remaining(&s->clienthello->ciphersuites);
}

size_t SSL_client_hello_get0_compression_methods(SSL *s, const unsigned char **out)
{
    if (s->clienthello == NULL)
        return 0;
    if (out != NULL)
        *out = s->clienthello->compressions;
    return s->clienthello->compressions_len;
}

int SSL_client_hello_get1_extensions_present(SSL *s, int **out, size_t *outlen)
{
    RAW_EXTENSION *ext;
    int *present;
    size_t num = 0, i;

    if (s->clienthello == NULL || out == NULL || outlen == NULL)
        return 0;
    for (i = 0; i < s->clienthello->pre_proc_exts_len; i++) {
        ext = s->clienthello->pre_proc_exts + i;
        if (ext->present)
            num++;
    }
    if ((present = OPENSSL_malloc(sizeof(*present) * num)) == NULL) {
        SSLerr(SSL_F_SSL_CLIENT_HELLO_GET1_EXTENSIONS_PRESENT,
               ERR_R_MALLOC_FAILURE);
        return 0;
    }
    for (i = 0; i < s->clienthello->pre_proc_exts_len; i++) {
        ext = s->clienthello->pre_proc_exts + i;
        if (ext->present) {
            if (ext->received_order >= num)
                goto err;
            present[ext->received_order] = ext->type;
        }
    }
    *out = present;
    *outlen = num;
    return 1;
 err:
    OPENSSL_free(present);
    return 0;
}

int SSL_client_hello_get0_ext(SSL *s, unsigned int type, const unsigned char **out,
                       size_t *outlen)
{
    size_t i;
    RAW_EXTENSION *r;

    if (s->clienthello == NULL)
        return 0;
    for (i = 0; i < s->clienthello->pre_proc_exts_len; ++i) {
        r = s->clienthello->pre_proc_exts + i;
        if (r->present && r->type == type) {
            if (out != NULL)
                *out = PACKET_data(&r->data);
            if (outlen != NULL)
                *outlen = PACKET_remaining(&r->data);
            return 1;
        }
    }
    return 0;
}

int SSL_free_buffers(SSL *ssl)
{
    RECORD_LAYER *rl = &ssl->rlayer;

    if (RECORD_LAYER_read_pending(rl) || RECORD_LAYER_write_pending(rl))
        return 0;

    RECORD_LAYER_release(rl);
    return 1;
}

int SSL_alloc_buffers(SSL *ssl)
{
    return ssl3_setup_buffers(ssl);
}

void SSL_CTX_set_keylog_callback(SSL_CTX *ctx, SSL_CTX_keylog_cb_func cb)
{
    ctx->keylog_callback = cb;
}

SSL_CTX_keylog_cb_func SSL_CTX_get_keylog_callback(const SSL_CTX *ctx)
{
    return ctx->keylog_callback;
}

static int nss_keylog_int(const char *prefix,
                          SSL *ssl,
                          const uint8_t *parameter_1,
                          size_t parameter_1_len,
                          const uint8_t *parameter_2,
                          size_t parameter_2_len)
{
    char *out = NULL;
    char *cursor = NULL;
    size_t out_len = 0;
    size_t i;
    size_t prefix_len;

    if (ssl->ctx->keylog_callback == NULL)
        return 1;

    /*
     * Our output buffer will contain the following strings, rendered with
     * space characters in between, terminated by a NULL character: first the
     * prefix, then the first parameter, then the second parameter. The
     * meaning of each parameter depends on the specific key material being
     * logged. Note that the first and second parameters are encoded in
     * hexadecimal, so we need a buffer that is twice their lengths.
     */
    prefix_len = strlen(prefix);
    out_len = prefix_len + (2 * parameter_1_len) + (2 * parameter_2_len) + 3;
    if ((out = cursor = OPENSSL_malloc(out_len)) == NULL) {
        SSLfatal(ssl, SSL_AD_INTERNAL_ERROR, SSL_F_NSS_KEYLOG_INT,
                 ERR_R_MALLOC_FAILURE);
        return 0;
    }

    strcpy(cursor, prefix);
    cursor += prefix_len;
    *cursor++ = ' ';

    for (i = 0; i < parameter_1_len; i++) {
        sprintf(cursor, "%02x", parameter_1[i]);
        cursor += 2;
    }
    *cursor++ = ' ';

    for (i = 0; i < parameter_2_len; i++) {
        sprintf(cursor, "%02x", parameter_2[i]);
        cursor += 2;
    }
    *cursor = '\0';

    ssl->ctx->keylog_callback(ssl, (const char *)out);
    OPENSSL_clear_free(out, out_len);
    return 1;

}

int ssl_log_rsa_client_key_exchange(SSL *ssl,
                                    const uint8_t *encrypted_premaster,
                                    size_t encrypted_premaster_len,
                                    const uint8_t *premaster,
                                    size_t premaster_len)
{
    if (encrypted_premaster_len < 8) {
        SSLfatal(ssl, SSL_AD_INTERNAL_ERROR,
                 SSL_F_SSL_LOG_RSA_CLIENT_KEY_EXCHANGE, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    /* We only want the first 8 bytes of the encrypted premaster as a tag. */
    return nss_keylog_int("RSA",
                          ssl,
                          encrypted_premaster,
                          8,
                          premaster,
                          premaster_len);
}

int ssl_log_secret(SSL *ssl,
                   const char *label,
                   const uint8_t *secret,
                   size_t secret_len)
{
    return nss_keylog_int(label,
                          ssl,
                          ssl->s3->client_random,
                          SSL3_RANDOM_SIZE,
                          secret,
                          secret_len);
}

#define SSLV2_CIPHER_LEN    3

int ssl_cache_cipherlist(SSL *s, PACKET *cipher_suites, int sslv2format)
{
    int n;

    n = sslv2format ? SSLV2_CIPHER_LEN : TLS_CIPHER_LEN;

    if (PACKET_remaining(cipher_suites) == 0) {
        SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER, SSL_F_SSL_CACHE_CIPHERLIST,
                 SSL_R_NO_CIPHERS_SPECIFIED);
        return 0;
    }

    if (PACKET_remaining(cipher_suites) % n != 0) {
        SSLfatal(s, SSL_AD_DECODE_ERROR, SSL_F_SSL_CACHE_CIPHERLIST,
                 SSL_R_ERROR_IN_RECEIVED_CIPHER_LIST);
        return 0;
    }

    OPENSSL_free(s->s3->tmp.ciphers_raw);
    s->s3->tmp.ciphers_raw = NULL;
    s->s3->tmp.ciphers_rawlen = 0;

    if (sslv2format) {
        size_t numciphers = PACKET_remaining(cipher_suites) / n;
        PACKET sslv2ciphers = *cipher_suites;
        unsigned int leadbyte;
        unsigned char *raw;

        /*
         * We store the raw ciphers list in SSLv3+ format so we need to do some
         * preprocessing to convert the list first. If there are any SSLv2 only
         * ciphersuites with a non-zero leading byte then we are going to
         * slightly over allocate because we won't store those. But that isn't a
         * problem.
         */
        raw = OPENSSL_malloc(numciphers * TLS_CIPHER_LEN);
        s->s3->tmp.ciphers_raw = raw;
        if (raw == NULL) {
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_SSL_CACHE_CIPHERLIST,
                     ERR_R_MALLOC_FAILURE);
            return 0;
        }
        for (s->s3->tmp.ciphers_rawlen = 0;
             PACKET_remaining(&sslv2ciphers) > 0;
             raw += TLS_CIPHER_LEN) {
            if (!PACKET_get_1(&sslv2ciphers, &leadbyte)
                    || (leadbyte == 0
                        && !PACKET_copy_bytes(&sslv2ciphers, raw,
                                              TLS_CIPHER_LEN))
                    || (leadbyte != 0
                        && !PACKET_forward(&sslv2ciphers, TLS_CIPHER_LEN))) {
                SSLfatal(s, SSL_AD_DECODE_ERROR, SSL_F_SSL_CACHE_CIPHERLIST,
                         SSL_R_BAD_PACKET);
                OPENSSL_free(s->s3->tmp.ciphers_raw);
                s->s3->tmp.ciphers_raw = NULL;
                s->s3->tmp.ciphers_rawlen = 0;
                return 0;
            }
            if (leadbyte == 0)
                s->s3->tmp.ciphers_rawlen += TLS_CIPHER_LEN;
        }
    } else if (!PACKET_memdup(cipher_suites, &s->s3->tmp.ciphers_raw,
                           &s->s3->tmp.ciphers_rawlen)) {
        SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_SSL_CACHE_CIPHERLIST,
                 ERR_R_INTERNAL_ERROR);
        return 0;
    }
    return 1;
}

int SSL_bytes_to_cipher_list(SSL *s, const unsigned char *bytes, size_t len,
                             int isv2format, STACK_OF(SSL_CIPHER) **sk,
                             STACK_OF(SSL_CIPHER) **scsvs)
{
    PACKET pkt;

    if (!PACKET_buf_init(&pkt, bytes, len))
        return 0;
    return bytes_to_cipher_list(s, &pkt, sk, scsvs, isv2format, 0);
}

int bytes_to_cipher_list(SSL *s, PACKET *cipher_suites,
                         STACK_OF(SSL_CIPHER) **skp,
                         STACK_OF(SSL_CIPHER) **scsvs_out,
                         int sslv2format, int fatal)
{
    const SSL_CIPHER *c;
    STACK_OF(SSL_CIPHER) *sk = NULL;
    STACK_OF(SSL_CIPHER) *scsvs = NULL;
    int n;
    /* 3 = SSLV2_CIPHER_LEN > TLS_CIPHER_LEN = 2. */
    unsigned char cipher[SSLV2_CIPHER_LEN];

    n = sslv2format ? SSLV2_CIPHER_LEN : TLS_CIPHER_LEN;

    if (PACKET_remaining(cipher_suites) == 0) {
        if (fatal)
            SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER, SSL_F_BYTES_TO_CIPHER_LIST,
                     SSL_R_NO_CIPHERS_SPECIFIED);
        else
            SSLerr(SSL_F_BYTES_TO_CIPHER_LIST, SSL_R_NO_CIPHERS_SPECIFIED);
        return 0;
    }

    if (PACKET_remaining(cipher_suites) % n != 0) {
        if (fatal)
            SSLfatal(s, SSL_AD_DECODE_ERROR, SSL_F_BYTES_TO_CIPHER_LIST,
                     SSL_R_ERROR_IN_RECEIVED_CIPHER_LIST);
        else
            SSLerr(SSL_F_BYTES_TO_CIPHER_LIST,
                   SSL_R_ERROR_IN_RECEIVED_CIPHER_LIST);
        return 0;
    }

    sk = sk_SSL_CIPHER_new_null();
    scsvs = sk_SSL_CIPHER_new_null();
    if (sk == NULL || scsvs == NULL) {
        if (fatal)
            SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_BYTES_TO_CIPHER_LIST,
                     ERR_R_MALLOC_FAILURE);
        else
            SSLerr(SSL_F_BYTES_TO_CIPHER_LIST, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    while (PACKET_copy_bytes(cipher_suites, cipher, n)) {
        /*
         * SSLv3 ciphers wrapped in an SSLv2-compatible ClientHello have the
         * first byte set to zero, while true SSLv2 ciphers have a non-zero
         * first byte. We don't support any true SSLv2 ciphers, so skip them.
         */
        if (sslv2format && cipher[0] != '\0')
            continue;

        /* For SSLv2-compat, ignore leading 0-byte. */
        c = ssl_get_cipher_by_char(s, sslv2format ? &cipher[1] : cipher, 1);
        if (c != NULL) {
            if ((c->valid && !sk_SSL_CIPHER_push(sk, c)) ||
                (!c->valid && !sk_SSL_CIPHER_push(scsvs, c))) {
                if (fatal)
                    SSLfatal(s, SSL_AD_INTERNAL_ERROR,
                             SSL_F_BYTES_TO_CIPHER_LIST, ERR_R_MALLOC_FAILURE);
                else
                    SSLerr(SSL_F_BYTES_TO_CIPHER_LIST, ERR_R_MALLOC_FAILURE);
                goto err;
            }
        }
    }
    if (PACKET_remaining(cipher_suites) > 0) {
        if (fatal)
            SSLfatal(s, SSL_AD_DECODE_ERROR, SSL_F_BYTES_TO_CIPHER_LIST,
                     SSL_R_BAD_LENGTH);
        else
            SSLerr(SSL_F_BYTES_TO_CIPHER_LIST, SSL_R_BAD_LENGTH);
        goto err;
    }

    if (skp != NULL)
        *skp = sk;
    else
        sk_SSL_CIPHER_free(sk);
    if (scsvs_out != NULL)
        *scsvs_out = scsvs;
    else
        sk_SSL_CIPHER_free(scsvs);
    return 1;
 err:
    sk_SSL_CIPHER_free(sk);
    sk_SSL_CIPHER_free(scsvs);
    return 0;
}

int SSL_CTX_set_max_early_data(SSL_CTX *ctx, uint32_t max_early_data)
{
    ctx->max_early_data = max_early_data;

    return 1;
}

uint32_t SSL_CTX_get_max_early_data(const SSL_CTX *ctx)
{
    return ctx->max_early_data;
}

int SSL_set_max_early_data(SSL *s, uint32_t max_early_data)
{
    s->max_early_data = max_early_data;

    return 1;
}

uint32_t SSL_get_max_early_data(const SSL *s)
{
    return s->max_early_data;
}

int SSL_CTX_set_recv_max_early_data(SSL_CTX *ctx, uint32_t recv_max_early_data)
{
    ctx->recv_max_early_data = recv_max_early_data;

    return 1;
}

uint32_t SSL_CTX_get_recv_max_early_data(const SSL_CTX *ctx)
{
    return ctx->recv_max_early_data;
}

int SSL_set_recv_max_early_data(SSL *s, uint32_t recv_max_early_data)
{
    s->recv_max_early_data = recv_max_early_data;

    return 1;
}

uint32_t SSL_get_recv_max_early_data(const SSL *s)
{
    return s->recv_max_early_data;
}

__owur unsigned int ssl_get_max_send_fragment(const SSL *ssl)
{
    /* Return any active Max Fragment Len extension */
    if (ssl->session != NULL && USE_MAX_FRAGMENT_LENGTH_EXT(ssl->session))
        return GET_MAX_FRAGMENT_LENGTH(ssl->session);

    /* return current SSL connection setting */
    return ssl->max_send_fragment;
}

__owur unsigned int ssl_get_split_send_fragment(const SSL *ssl)
{
    /* Return a value regarding an active Max Fragment Len extension */
    if (ssl->session != NULL && USE_MAX_FRAGMENT_LENGTH_EXT(ssl->session)
        && ssl->split_send_fragment > GET_MAX_FRAGMENT_LENGTH(ssl->session))
        return GET_MAX_FRAGMENT_LENGTH(ssl->session);

    /* else limit |split_send_fragment| to current |max_send_fragment| */
    if (ssl->split_send_fragment > ssl->max_send_fragment)
        return ssl->max_send_fragment;

    /* return current SSL connection setting */
    return ssl->split_send_fragment;
}

int SSL_stateless(SSL *s)
{
    int ret;

    /* Ensure there is no state left over from a previous invocation */
    if (!SSL_clear(s))
        return 0;

    ERR_clear_error();

    s->s3->flags |= TLS1_FLAGS_STATELESS;
    ret = SSL_accept(s);
    s->s3->flags &= ~TLS1_FLAGS_STATELESS;

    if (ret > 0 && s->ext.cookieok)
        return 1;

    if (s->hello_retry_request == SSL_HRR_PENDING && !ossl_statem_in_error(s))
        return 0;

    return -1;
}

void SSL_CTX_set_post_handshake_auth(SSL_CTX *ctx, int val)
{
    ctx->pha_enabled = val;
}

void SSL_set_post_handshake_auth(SSL *ssl, int val)
{
    ssl->pha_enabled = val;
}

int SSL_verify_client_post_handshake(SSL *ssl)
{
    if (!SSL_IS_TLS13(ssl)) {
        SSLerr(SSL_F_SSL_VERIFY_CLIENT_POST_HANDSHAKE, SSL_R_WRONG_SSL_VERSION);
        return 0;
    }
    if (!ssl->server) {
        SSLerr(SSL_F_SSL_VERIFY_CLIENT_POST_HANDSHAKE, SSL_R_NOT_SERVER);
        return 0;
    }

    if (!SSL_is_init_finished(ssl)) {
        SSLerr(SSL_F_SSL_VERIFY_CLIENT_POST_HANDSHAKE, SSL_R_STILL_IN_INIT);
        return 0;
    }

    switch (ssl->post_handshake_auth) {
    case SSL_PHA_NONE:
        SSLerr(SSL_F_SSL_VERIFY_CLIENT_POST_HANDSHAKE, SSL_R_EXTENSION_NOT_RECEIVED);
        return 0;
    default:
    case SSL_PHA_EXT_SENT:
        SSLerr(SSL_F_SSL_VERIFY_CLIENT_POST_HANDSHAKE, ERR_R_INTERNAL_ERROR);
        return 0;
    case SSL_PHA_EXT_RECEIVED:
        break;
    case SSL_PHA_REQUEST_PENDING:
        SSLerr(SSL_F_SSL_VERIFY_CLIENT_POST_HANDSHAKE, SSL_R_REQUEST_PENDING);
        return 0;
    case SSL_PHA_REQUESTED:
        SSLerr(SSL_F_SSL_VERIFY_CLIENT_POST_HANDSHAKE, SSL_R_REQUEST_SENT);
        return 0;
    }

    ssl->post_handshake_auth = SSL_PHA_REQUEST_PENDING;

    /* checks verify_mode and algorithm_auth */
    if (!send_certificate_request(ssl)) {
        ssl->post_handshake_auth = SSL_PHA_EXT_RECEIVED; /* restore on error */
        SSLerr(SSL_F_SSL_VERIFY_CLIENT_POST_HANDSHAKE, SSL_R_INVALID_CONFIG);
        return 0;
    }

    ossl_statem_set_in_init(ssl, 1);
    return 1;
}

int SSL_CTX_set_session_ticket_cb(SSL_CTX *ctx,
                                  SSL_CTX_generate_session_ticket_fn gen_cb,
                                  SSL_CTX_decrypt_session_ticket_fn dec_cb,
                                  void *arg)
{
    ctx->generate_ticket_cb = gen_cb;
    ctx->decrypt_ticket_cb = dec_cb;
    ctx->ticket_cb_data = arg;
    return 1;
}

void SSL_CTX_set_allow_early_data_cb(SSL_CTX *ctx,
                                     SSL_allow_early_data_cb_fn cb,
                                     void *arg)
{
    ctx->allow_early_data_cb = cb;
    ctx->allow_early_data_cb_data = arg;
}

void SSL_set_allow_early_data_cb(SSL *s,
                                 SSL_allow_early_data_cb_fn cb,
                                 void *arg)
{
    s->allow_early_data_cb = cb;
    s->allow_early_data_cb_data = arg;
}
