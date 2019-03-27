/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright (c) 2002, Oracle and/or its affiliates. All rights reserved
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <sys/types.h>

#include "internal/nelem.h"
#include "internal/o_dir.h"
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <openssl/dh.h>
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include "internal/refcount.h"
#include "ssl_locl.h"
#include "ssl_cert_table.h"
#include "internal/thread_once.h"

static int ssl_security_default_callback(const SSL *s, const SSL_CTX *ctx,
                                         int op, int bits, int nid, void *other,
                                         void *ex);

static CRYPTO_ONCE ssl_x509_store_ctx_once = CRYPTO_ONCE_STATIC_INIT;
static volatile int ssl_x509_store_ctx_idx = -1;

DEFINE_RUN_ONCE_STATIC(ssl_x509_store_ctx_init)
{
    ssl_x509_store_ctx_idx = X509_STORE_CTX_get_ex_new_index(0,
                                                             "SSL for verify callback",
                                                             NULL, NULL, NULL);
    return ssl_x509_store_ctx_idx >= 0;
}

int SSL_get_ex_data_X509_STORE_CTX_idx(void)
{

    if (!RUN_ONCE(&ssl_x509_store_ctx_once, ssl_x509_store_ctx_init))
        return -1;
    return ssl_x509_store_ctx_idx;
}

CERT *ssl_cert_new(void)
{
    CERT *ret = OPENSSL_zalloc(sizeof(*ret));

    if (ret == NULL) {
        SSLerr(SSL_F_SSL_CERT_NEW, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    ret->key = &(ret->pkeys[SSL_PKEY_RSA]);
    ret->references = 1;
    ret->sec_cb = ssl_security_default_callback;
    ret->sec_level = OPENSSL_TLS_SECURITY_LEVEL;
    ret->sec_ex = NULL;
    ret->lock = CRYPTO_THREAD_lock_new();
    if (ret->lock == NULL) {
        SSLerr(SSL_F_SSL_CERT_NEW, ERR_R_MALLOC_FAILURE);
        OPENSSL_free(ret);
        return NULL;
    }

    return ret;
}

CERT *ssl_cert_dup(CERT *cert)
{
    CERT *ret = OPENSSL_zalloc(sizeof(*ret));
    int i;

    if (ret == NULL) {
        SSLerr(SSL_F_SSL_CERT_DUP, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    ret->references = 1;
    ret->key = &ret->pkeys[cert->key - cert->pkeys];
    ret->lock = CRYPTO_THREAD_lock_new();
    if (ret->lock == NULL) {
        SSLerr(SSL_F_SSL_CERT_DUP, ERR_R_MALLOC_FAILURE);
        OPENSSL_free(ret);
        return NULL;
    }
#ifndef OPENSSL_NO_DH
    if (cert->dh_tmp != NULL) {
        ret->dh_tmp = cert->dh_tmp;
        EVP_PKEY_up_ref(ret->dh_tmp);
    }
    ret->dh_tmp_cb = cert->dh_tmp_cb;
    ret->dh_tmp_auto = cert->dh_tmp_auto;
#endif

    for (i = 0; i < SSL_PKEY_NUM; i++) {
        CERT_PKEY *cpk = cert->pkeys + i;
        CERT_PKEY *rpk = ret->pkeys + i;
        if (cpk->x509 != NULL) {
            rpk->x509 = cpk->x509;
            X509_up_ref(rpk->x509);
        }

        if (cpk->privatekey != NULL) {
            rpk->privatekey = cpk->privatekey;
            EVP_PKEY_up_ref(cpk->privatekey);
        }

        if (cpk->chain) {
            rpk->chain = X509_chain_up_ref(cpk->chain);
            if (!rpk->chain) {
                SSLerr(SSL_F_SSL_CERT_DUP, ERR_R_MALLOC_FAILURE);
                goto err;
            }
        }
        if (cert->pkeys[i].serverinfo != NULL) {
            /* Just copy everything. */
            ret->pkeys[i].serverinfo =
                OPENSSL_malloc(cert->pkeys[i].serverinfo_length);
            if (ret->pkeys[i].serverinfo == NULL) {
                SSLerr(SSL_F_SSL_CERT_DUP, ERR_R_MALLOC_FAILURE);
                goto err;
            }
            ret->pkeys[i].serverinfo_length = cert->pkeys[i].serverinfo_length;
            memcpy(ret->pkeys[i].serverinfo,
                   cert->pkeys[i].serverinfo, cert->pkeys[i].serverinfo_length);
        }
    }

    /* Configured sigalgs copied across */
    if (cert->conf_sigalgs) {
        ret->conf_sigalgs = OPENSSL_malloc(cert->conf_sigalgslen
                                           * sizeof(*cert->conf_sigalgs));
        if (ret->conf_sigalgs == NULL)
            goto err;
        memcpy(ret->conf_sigalgs, cert->conf_sigalgs,
               cert->conf_sigalgslen * sizeof(*cert->conf_sigalgs));
        ret->conf_sigalgslen = cert->conf_sigalgslen;
    } else
        ret->conf_sigalgs = NULL;

    if (cert->client_sigalgs) {
        ret->client_sigalgs = OPENSSL_malloc(cert->client_sigalgslen
                                             * sizeof(*cert->client_sigalgs));
        if (ret->client_sigalgs == NULL)
            goto err;
        memcpy(ret->client_sigalgs, cert->client_sigalgs,
               cert->client_sigalgslen * sizeof(*cert->client_sigalgs));
        ret->client_sigalgslen = cert->client_sigalgslen;
    } else
        ret->client_sigalgs = NULL;
    /* Shared sigalgs also NULL */
    ret->shared_sigalgs = NULL;
    /* Copy any custom client certificate types */
    if (cert->ctype) {
        ret->ctype = OPENSSL_memdup(cert->ctype, cert->ctype_len);
        if (ret->ctype == NULL)
            goto err;
        ret->ctype_len = cert->ctype_len;
    }

    ret->cert_flags = cert->cert_flags;

    ret->cert_cb = cert->cert_cb;
    ret->cert_cb_arg = cert->cert_cb_arg;

    if (cert->verify_store) {
        X509_STORE_up_ref(cert->verify_store);
        ret->verify_store = cert->verify_store;
    }

    if (cert->chain_store) {
        X509_STORE_up_ref(cert->chain_store);
        ret->chain_store = cert->chain_store;
    }

    ret->sec_cb = cert->sec_cb;
    ret->sec_level = cert->sec_level;
    ret->sec_ex = cert->sec_ex;

    if (!custom_exts_copy(&ret->custext, &cert->custext))
        goto err;
#ifndef OPENSSL_NO_PSK
    if (cert->psk_identity_hint) {
        ret->psk_identity_hint = OPENSSL_strdup(cert->psk_identity_hint);
        if (ret->psk_identity_hint == NULL)
            goto err;
    }
#endif
    return ret;

 err:
    ssl_cert_free(ret);

    return NULL;
}

/* Free up and clear all certificates and chains */

void ssl_cert_clear_certs(CERT *c)
{
    int i;
    if (c == NULL)
        return;
    for (i = 0; i < SSL_PKEY_NUM; i++) {
        CERT_PKEY *cpk = c->pkeys + i;
        X509_free(cpk->x509);
        cpk->x509 = NULL;
        EVP_PKEY_free(cpk->privatekey);
        cpk->privatekey = NULL;
        sk_X509_pop_free(cpk->chain, X509_free);
        cpk->chain = NULL;
        OPENSSL_free(cpk->serverinfo);
        cpk->serverinfo = NULL;
        cpk->serverinfo_length = 0;
    }
}

void ssl_cert_free(CERT *c)
{
    int i;

    if (c == NULL)
        return;
    CRYPTO_DOWN_REF(&c->references, &i, c->lock);
    REF_PRINT_COUNT("CERT", c);
    if (i > 0)
        return;
    REF_ASSERT_ISNT(i < 0);

#ifndef OPENSSL_NO_DH
    EVP_PKEY_free(c->dh_tmp);
#endif

    ssl_cert_clear_certs(c);
    OPENSSL_free(c->conf_sigalgs);
    OPENSSL_free(c->client_sigalgs);
    OPENSSL_free(c->shared_sigalgs);
    OPENSSL_free(c->ctype);
    X509_STORE_free(c->verify_store);
    X509_STORE_free(c->chain_store);
    custom_exts_free(&c->custext);
#ifndef OPENSSL_NO_PSK
    OPENSSL_free(c->psk_identity_hint);
#endif
    CRYPTO_THREAD_lock_free(c->lock);
    OPENSSL_free(c);
}

int ssl_cert_set0_chain(SSL *s, SSL_CTX *ctx, STACK_OF(X509) *chain)
{
    int i, r;
    CERT_PKEY *cpk = s ? s->cert->key : ctx->cert->key;
    if (!cpk)
        return 0;
    for (i = 0; i < sk_X509_num(chain); i++) {
        r = ssl_security_cert(s, ctx, sk_X509_value(chain, i), 0, 0);
        if (r != 1) {
            SSLerr(SSL_F_SSL_CERT_SET0_CHAIN, r);
            return 0;
        }
    }
    sk_X509_pop_free(cpk->chain, X509_free);
    cpk->chain = chain;
    return 1;
}

int ssl_cert_set1_chain(SSL *s, SSL_CTX *ctx, STACK_OF(X509) *chain)
{
    STACK_OF(X509) *dchain;
    if (!chain)
        return ssl_cert_set0_chain(s, ctx, NULL);
    dchain = X509_chain_up_ref(chain);
    if (!dchain)
        return 0;
    if (!ssl_cert_set0_chain(s, ctx, dchain)) {
        sk_X509_pop_free(dchain, X509_free);
        return 0;
    }
    return 1;
}

int ssl_cert_add0_chain_cert(SSL *s, SSL_CTX *ctx, X509 *x)
{
    int r;
    CERT_PKEY *cpk = s ? s->cert->key : ctx->cert->key;
    if (!cpk)
        return 0;
    r = ssl_security_cert(s, ctx, x, 0, 0);
    if (r != 1) {
        SSLerr(SSL_F_SSL_CERT_ADD0_CHAIN_CERT, r);
        return 0;
    }
    if (!cpk->chain)
        cpk->chain = sk_X509_new_null();
    if (!cpk->chain || !sk_X509_push(cpk->chain, x))
        return 0;
    return 1;
}

int ssl_cert_add1_chain_cert(SSL *s, SSL_CTX *ctx, X509 *x)
{
    if (!ssl_cert_add0_chain_cert(s, ctx, x))
        return 0;
    X509_up_ref(x);
    return 1;
}

int ssl_cert_select_current(CERT *c, X509 *x)
{
    int i;
    if (x == NULL)
        return 0;
    for (i = 0; i < SSL_PKEY_NUM; i++) {
        CERT_PKEY *cpk = c->pkeys + i;
        if (cpk->x509 == x && cpk->privatekey) {
            c->key = cpk;
            return 1;
        }
    }

    for (i = 0; i < SSL_PKEY_NUM; i++) {
        CERT_PKEY *cpk = c->pkeys + i;
        if (cpk->privatekey && cpk->x509 && !X509_cmp(cpk->x509, x)) {
            c->key = cpk;
            return 1;
        }
    }
    return 0;
}

int ssl_cert_set_current(CERT *c, long op)
{
    int i, idx;
    if (!c)
        return 0;
    if (op == SSL_CERT_SET_FIRST)
        idx = 0;
    else if (op == SSL_CERT_SET_NEXT) {
        idx = (int)(c->key - c->pkeys + 1);
        if (idx >= SSL_PKEY_NUM)
            return 0;
    } else
        return 0;
    for (i = idx; i < SSL_PKEY_NUM; i++) {
        CERT_PKEY *cpk = c->pkeys + i;
        if (cpk->x509 && cpk->privatekey) {
            c->key = cpk;
            return 1;
        }
    }
    return 0;
}

void ssl_cert_set_cert_cb(CERT *c, int (*cb) (SSL *ssl, void *arg), void *arg)
{
    c->cert_cb = cb;
    c->cert_cb_arg = arg;
}

int ssl_verify_cert_chain(SSL *s, STACK_OF(X509) *sk)
{
    X509 *x;
    int i = 0;
    X509_STORE *verify_store;
    X509_STORE_CTX *ctx = NULL;
    X509_VERIFY_PARAM *param;

    if ((sk == NULL) || (sk_X509_num(sk) == 0))
        return 0;

    if (s->cert->verify_store)
        verify_store = s->cert->verify_store;
    else
        verify_store = s->ctx->cert_store;

    ctx = X509_STORE_CTX_new();
    if (ctx == NULL) {
        SSLerr(SSL_F_SSL_VERIFY_CERT_CHAIN, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    x = sk_X509_value(sk, 0);
    if (!X509_STORE_CTX_init(ctx, verify_store, x, sk)) {
        SSLerr(SSL_F_SSL_VERIFY_CERT_CHAIN, ERR_R_X509_LIB);
        goto end;
    }
    param = X509_STORE_CTX_get0_param(ctx);
    /*
     * XXX: Separate @AUTHSECLEVEL and @TLSSECLEVEL would be useful at some
     * point, for now a single @SECLEVEL sets the same policy for TLS crypto
     * and PKI authentication.
     */
    X509_VERIFY_PARAM_set_auth_level(param, SSL_get_security_level(s));

    /* Set suite B flags if needed */
    X509_STORE_CTX_set_flags(ctx, tls1_suiteb(s));
    if (!X509_STORE_CTX_set_ex_data
        (ctx, SSL_get_ex_data_X509_STORE_CTX_idx(), s)) {
        goto end;
    }

    /* Verify via DANE if enabled */
    if (DANETLS_ENABLED(&s->dane))
        X509_STORE_CTX_set0_dane(ctx, &s->dane);

    /*
     * We need to inherit the verify parameters. These can be determined by
     * the context: if its a server it will verify SSL client certificates or
     * vice versa.
     */

    X509_STORE_CTX_set_default(ctx, s->server ? "ssl_client" : "ssl_server");
    /*
     * Anything non-default in "s->param" should overwrite anything in the ctx.
     */
    X509_VERIFY_PARAM_set1(param, s->param);

    if (s->verify_callback)
        X509_STORE_CTX_set_verify_cb(ctx, s->verify_callback);

    if (s->ctx->app_verify_callback != NULL)
        i = s->ctx->app_verify_callback(ctx, s->ctx->app_verify_arg);
    else
        i = X509_verify_cert(ctx);

    s->verify_result = X509_STORE_CTX_get_error(ctx);
    sk_X509_pop_free(s->verified_chain, X509_free);
    s->verified_chain = NULL;
    if (X509_STORE_CTX_get0_chain(ctx) != NULL) {
        s->verified_chain = X509_STORE_CTX_get1_chain(ctx);
        if (s->verified_chain == NULL) {
            SSLerr(SSL_F_SSL_VERIFY_CERT_CHAIN, ERR_R_MALLOC_FAILURE);
            i = 0;
        }
    }

    /* Move peername from the store context params to the SSL handle's */
    X509_VERIFY_PARAM_move_peername(s->param, param);

 end:
    X509_STORE_CTX_free(ctx);
    return i;
}

static void set0_CA_list(STACK_OF(X509_NAME) **ca_list,
                        STACK_OF(X509_NAME) *name_list)
{
    sk_X509_NAME_pop_free(*ca_list, X509_NAME_free);
    *ca_list = name_list;
}

STACK_OF(X509_NAME) *SSL_dup_CA_list(const STACK_OF(X509_NAME) *sk)
{
    int i;
    const int num = sk_X509_NAME_num(sk);
    STACK_OF(X509_NAME) *ret;
    X509_NAME *name;

    ret = sk_X509_NAME_new_reserve(NULL, num);
    if (ret == NULL) {
        SSLerr(SSL_F_SSL_DUP_CA_LIST, ERR_R_MALLOC_FAILURE);
        return NULL;
    }
    for (i = 0; i < num; i++) {
        name = X509_NAME_dup(sk_X509_NAME_value(sk, i));
        if (name == NULL) {
            SSLerr(SSL_F_SSL_DUP_CA_LIST, ERR_R_MALLOC_FAILURE);
            sk_X509_NAME_pop_free(ret, X509_NAME_free);
            return NULL;
        }
        sk_X509_NAME_push(ret, name);   /* Cannot fail after reserve call */
    }
    return ret;
}

void SSL_set0_CA_list(SSL *s, STACK_OF(X509_NAME) *name_list)
{
    set0_CA_list(&s->ca_names, name_list);
}

void SSL_CTX_set0_CA_list(SSL_CTX *ctx, STACK_OF(X509_NAME) *name_list)
{
    set0_CA_list(&ctx->ca_names, name_list);
}

const STACK_OF(X509_NAME) *SSL_CTX_get0_CA_list(const SSL_CTX *ctx)
{
    return ctx->ca_names;
}

const STACK_OF(X509_NAME) *SSL_get0_CA_list(const SSL *s)
{
    return s->ca_names != NULL ? s->ca_names : s->ctx->ca_names;
}

void SSL_CTX_set_client_CA_list(SSL_CTX *ctx, STACK_OF(X509_NAME) *name_list)
{
    set0_CA_list(&ctx->client_ca_names, name_list);
}

STACK_OF(X509_NAME) *SSL_CTX_get_client_CA_list(const SSL_CTX *ctx)
{
    return ctx->client_ca_names;
}

void SSL_set_client_CA_list(SSL *s, STACK_OF(X509_NAME) *name_list)
{
    set0_CA_list(&s->client_ca_names, name_list);
}

const STACK_OF(X509_NAME) *SSL_get0_peer_CA_list(const SSL *s)
{
    return s->s3 != NULL ? s->s3->tmp.peer_ca_names : NULL;
}

STACK_OF(X509_NAME) *SSL_get_client_CA_list(const SSL *s)
{
    if (!s->server)
        return s->s3 != NULL ?  s->s3->tmp.peer_ca_names : NULL;
    return s->client_ca_names != NULL ?  s->client_ca_names
                                      : s->ctx->client_ca_names;
}

static int add_ca_name(STACK_OF(X509_NAME) **sk, const X509 *x)
{
    X509_NAME *name;

    if (x == NULL)
        return 0;
    if (*sk == NULL && ((*sk = sk_X509_NAME_new_null()) == NULL))
        return 0;

    if ((name = X509_NAME_dup(X509_get_subject_name(x))) == NULL)
        return 0;

    if (!sk_X509_NAME_push(*sk, name)) {
        X509_NAME_free(name);
        return 0;
    }
    return 1;
}

int SSL_add1_to_CA_list(SSL *ssl, const X509 *x)
{
    return add_ca_name(&ssl->ca_names, x);
}

int SSL_CTX_add1_to_CA_list(SSL_CTX *ctx, const X509 *x)
{
    return add_ca_name(&ctx->ca_names, x);
}

/*
 * The following two are older names are to be replaced with
 * SSL(_CTX)_add1_to_CA_list
 */
int SSL_add_client_CA(SSL *ssl, X509 *x)
{
    return add_ca_name(&ssl->client_ca_names, x);
}

int SSL_CTX_add_client_CA(SSL_CTX *ctx, X509 *x)
{
    return add_ca_name(&ctx->client_ca_names, x);
}

static int xname_cmp(const X509_NAME *a, const X509_NAME *b)
{
    unsigned char *abuf = NULL, *bbuf = NULL;
    int alen, blen, ret;

    /* X509_NAME_cmp() itself casts away constness in this way, so
     * assume it's safe:
     */
    alen = i2d_X509_NAME((X509_NAME *)a, &abuf);
    blen = i2d_X509_NAME((X509_NAME *)b, &bbuf);

    if (alen < 0 || blen < 0)
        ret = -2;
    else if (alen != blen)
        ret = alen - blen;
    else /* alen == blen */
        ret = memcmp(abuf, bbuf, alen);

    OPENSSL_free(abuf);
    OPENSSL_free(bbuf);

    return ret;
}

static int xname_sk_cmp(const X509_NAME *const *a, const X509_NAME *const *b)
{
    return xname_cmp(*a, *b);
}

static unsigned long xname_hash(const X509_NAME *a)
{
    return X509_NAME_hash((X509_NAME *)a);
}

/**
 * Load CA certs from a file into a ::STACK. Note that it is somewhat misnamed;
 * it doesn't really have anything to do with clients (except that a common use
 * for a stack of CAs is to send it to the client). Actually, it doesn't have
 * much to do with CAs, either, since it will load any old cert.
 * \param file the file containing one or more certs.
 * \return a ::STACK containing the certs.
 */
STACK_OF(X509_NAME) *SSL_load_client_CA_file(const char *file)
{
    BIO *in = BIO_new(BIO_s_file());
    X509 *x = NULL;
    X509_NAME *xn = NULL;
    STACK_OF(X509_NAME) *ret = NULL;
    LHASH_OF(X509_NAME) *name_hash = lh_X509_NAME_new(xname_hash, xname_cmp);

    if ((name_hash == NULL) || (in == NULL)) {
        SSLerr(SSL_F_SSL_LOAD_CLIENT_CA_FILE, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    if (!BIO_read_filename(in, file))
        goto err;

    for (;;) {
        if (PEM_read_bio_X509(in, &x, NULL, NULL) == NULL)
            break;
        if (ret == NULL) {
            ret = sk_X509_NAME_new_null();
            if (ret == NULL) {
                SSLerr(SSL_F_SSL_LOAD_CLIENT_CA_FILE, ERR_R_MALLOC_FAILURE);
                goto err;
            }
        }
        if ((xn = X509_get_subject_name(x)) == NULL)
            goto err;
        /* check for duplicates */
        xn = X509_NAME_dup(xn);
        if (xn == NULL)
            goto err;
        if (lh_X509_NAME_retrieve(name_hash, xn) != NULL) {
            /* Duplicate. */
            X509_NAME_free(xn);
            xn = NULL;
        } else {
            lh_X509_NAME_insert(name_hash, xn);
            if (!sk_X509_NAME_push(ret, xn))
                goto err;
        }
    }
    goto done;

 err:
    X509_NAME_free(xn);
    sk_X509_NAME_pop_free(ret, X509_NAME_free);
    ret = NULL;
 done:
    BIO_free(in);
    X509_free(x);
    lh_X509_NAME_free(name_hash);
    if (ret != NULL)
        ERR_clear_error();
    return ret;
}

/**
 * Add a file of certs to a stack.
 * \param stack the stack to add to.
 * \param file the file to add from. All certs in this file that are not
 * already in the stack will be added.
 * \return 1 for success, 0 for failure. Note that in the case of failure some
 * certs may have been added to \c stack.
 */

int SSL_add_file_cert_subjects_to_stack(STACK_OF(X509_NAME) *stack,
                                        const char *file)
{
    BIO *in;
    X509 *x = NULL;
    X509_NAME *xn = NULL;
    int ret = 1;
    int (*oldcmp) (const X509_NAME *const *a, const X509_NAME *const *b);

    oldcmp = sk_X509_NAME_set_cmp_func(stack, xname_sk_cmp);

    in = BIO_new(BIO_s_file());

    if (in == NULL) {
        SSLerr(SSL_F_SSL_ADD_FILE_CERT_SUBJECTS_TO_STACK, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    if (!BIO_read_filename(in, file))
        goto err;

    for (;;) {
        if (PEM_read_bio_X509(in, &x, NULL, NULL) == NULL)
            break;
        if ((xn = X509_get_subject_name(x)) == NULL)
            goto err;
        xn = X509_NAME_dup(xn);
        if (xn == NULL)
            goto err;
        if (sk_X509_NAME_find(stack, xn) >= 0) {
            /* Duplicate. */
            X509_NAME_free(xn);
        } else if (!sk_X509_NAME_push(stack, xn)) {
            X509_NAME_free(xn);
            goto err;
        }
    }

    ERR_clear_error();
    goto done;

 err:
    ret = 0;
 done:
    BIO_free(in);
    X509_free(x);
    (void)sk_X509_NAME_set_cmp_func(stack, oldcmp);
    return ret;
}

/**
 * Add a directory of certs to a stack.
 * \param stack the stack to append to.
 * \param dir the directory to append from. All files in this directory will be
 * examined as potential certs. Any that are acceptable to
 * SSL_add_dir_cert_subjects_to_stack() that are not already in the stack will be
 * included.
 * \return 1 for success, 0 for failure. Note that in the case of failure some
 * certs may have been added to \c stack.
 */

int SSL_add_dir_cert_subjects_to_stack(STACK_OF(X509_NAME) *stack,
                                       const char *dir)
{
    OPENSSL_DIR_CTX *d = NULL;
    const char *filename;
    int ret = 0;

    /* Note that a side effect is that the CAs will be sorted by name */

    while ((filename = OPENSSL_DIR_read(&d, dir))) {
        char buf[1024];
        int r;

        if (strlen(dir) + strlen(filename) + 2 > sizeof(buf)) {
            SSLerr(SSL_F_SSL_ADD_DIR_CERT_SUBJECTS_TO_STACK,
                   SSL_R_PATH_TOO_LONG);
            goto err;
        }
#ifdef OPENSSL_SYS_VMS
        r = BIO_snprintf(buf, sizeof(buf), "%s%s", dir, filename);
#else
        r = BIO_snprintf(buf, sizeof(buf), "%s/%s", dir, filename);
#endif
        if (r <= 0 || r >= (int)sizeof(buf))
            goto err;
        if (!SSL_add_file_cert_subjects_to_stack(stack, buf))
            goto err;
    }

    if (errno) {
        SYSerr(SYS_F_OPENDIR, get_last_sys_error());
        ERR_add_error_data(3, "OPENSSL_DIR_read(&ctx, '", dir, "')");
        SSLerr(SSL_F_SSL_ADD_DIR_CERT_SUBJECTS_TO_STACK, ERR_R_SYS_LIB);
        goto err;
    }

    ret = 1;

 err:
    if (d)
        OPENSSL_DIR_end(&d);

    return ret;
}

/* Build a certificate chain for current certificate */
int ssl_build_cert_chain(SSL *s, SSL_CTX *ctx, int flags)
{
    CERT *c = s ? s->cert : ctx->cert;
    CERT_PKEY *cpk = c->key;
    X509_STORE *chain_store = NULL;
    X509_STORE_CTX *xs_ctx = NULL;
    STACK_OF(X509) *chain = NULL, *untrusted = NULL;
    X509 *x;
    int i, rv = 0;

    if (!cpk->x509) {
        SSLerr(SSL_F_SSL_BUILD_CERT_CHAIN, SSL_R_NO_CERTIFICATE_SET);
        goto err;
    }
    /* Rearranging and check the chain: add everything to a store */
    if (flags & SSL_BUILD_CHAIN_FLAG_CHECK) {
        chain_store = X509_STORE_new();
        if (chain_store == NULL)
            goto err;
        for (i = 0; i < sk_X509_num(cpk->chain); i++) {
            x = sk_X509_value(cpk->chain, i);
            if (!X509_STORE_add_cert(chain_store, x))
                goto err;
        }
        /* Add EE cert too: it might be self signed */
        if (!X509_STORE_add_cert(chain_store, cpk->x509))
            goto err;
    } else {
        if (c->chain_store)
            chain_store = c->chain_store;
        else if (s)
            chain_store = s->ctx->cert_store;
        else
            chain_store = ctx->cert_store;

        if (flags & SSL_BUILD_CHAIN_FLAG_UNTRUSTED)
            untrusted = cpk->chain;
    }

    xs_ctx = X509_STORE_CTX_new();
    if (xs_ctx == NULL) {
        SSLerr(SSL_F_SSL_BUILD_CERT_CHAIN, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    if (!X509_STORE_CTX_init(xs_ctx, chain_store, cpk->x509, untrusted)) {
        SSLerr(SSL_F_SSL_BUILD_CERT_CHAIN, ERR_R_X509_LIB);
        goto err;
    }
    /* Set suite B flags if needed */
    X509_STORE_CTX_set_flags(xs_ctx,
                             c->cert_flags & SSL_CERT_FLAG_SUITEB_128_LOS);

    i = X509_verify_cert(xs_ctx);
    if (i <= 0 && flags & SSL_BUILD_CHAIN_FLAG_IGNORE_ERROR) {
        if (flags & SSL_BUILD_CHAIN_FLAG_CLEAR_ERROR)
            ERR_clear_error();
        i = 1;
        rv = 2;
    }
    if (i > 0)
        chain = X509_STORE_CTX_get1_chain(xs_ctx);
    if (i <= 0) {
        SSLerr(SSL_F_SSL_BUILD_CERT_CHAIN, SSL_R_CERTIFICATE_VERIFY_FAILED);
        i = X509_STORE_CTX_get_error(xs_ctx);
        ERR_add_error_data(2, "Verify error:",
                           X509_verify_cert_error_string(i));

        goto err;
    }
    /* Remove EE certificate from chain */
    x = sk_X509_shift(chain);
    X509_free(x);
    if (flags & SSL_BUILD_CHAIN_FLAG_NO_ROOT) {
        if (sk_X509_num(chain) > 0) {
            /* See if last cert is self signed */
            x = sk_X509_value(chain, sk_X509_num(chain) - 1);
            if (X509_get_extension_flags(x) & EXFLAG_SS) {
                x = sk_X509_pop(chain);
                X509_free(x);
            }
        }
    }
    /*
     * Check security level of all CA certificates: EE will have been checked
     * already.
     */
    for (i = 0; i < sk_X509_num(chain); i++) {
        x = sk_X509_value(chain, i);
        rv = ssl_security_cert(s, ctx, x, 0, 0);
        if (rv != 1) {
            SSLerr(SSL_F_SSL_BUILD_CERT_CHAIN, rv);
            sk_X509_pop_free(chain, X509_free);
            rv = 0;
            goto err;
        }
    }
    sk_X509_pop_free(cpk->chain, X509_free);
    cpk->chain = chain;
    if (rv == 0)
        rv = 1;
 err:
    if (flags & SSL_BUILD_CHAIN_FLAG_CHECK)
        X509_STORE_free(chain_store);
    X509_STORE_CTX_free(xs_ctx);

    return rv;
}

int ssl_cert_set_cert_store(CERT *c, X509_STORE *store, int chain, int ref)
{
    X509_STORE **pstore;
    if (chain)
        pstore = &c->chain_store;
    else
        pstore = &c->verify_store;
    X509_STORE_free(*pstore);
    *pstore = store;
    if (ref && store)
        X509_STORE_up_ref(store);
    return 1;
}

static int ssl_security_default_callback(const SSL *s, const SSL_CTX *ctx,
                                         int op, int bits, int nid, void *other,
                                         void *ex)
{
    int level, minbits;
    static const int minbits_table[5] = { 80, 112, 128, 192, 256 };
    if (ctx)
        level = SSL_CTX_get_security_level(ctx);
    else
        level = SSL_get_security_level(s);

    if (level <= 0) {
        /*
         * No EDH keys weaker than 1024-bits even at level 0, otherwise,
         * anything goes.
         */
        if (op == SSL_SECOP_TMP_DH && bits < 80)
            return 0;
        return 1;
    }
    if (level > 5)
        level = 5;
    minbits = minbits_table[level - 1];
    switch (op) {
    case SSL_SECOP_CIPHER_SUPPORTED:
    case SSL_SECOP_CIPHER_SHARED:
    case SSL_SECOP_CIPHER_CHECK:
        {
            const SSL_CIPHER *c = other;
            /* No ciphers below security level */
            if (bits < minbits)
                return 0;
            /* No unauthenticated ciphersuites */
            if (c->algorithm_auth & SSL_aNULL)
                return 0;
            /* No MD5 mac ciphersuites */
            if (c->algorithm_mac & SSL_MD5)
                return 0;
            /* SHA1 HMAC is 160 bits of security */
            if (minbits > 160 && c->algorithm_mac & SSL_SHA1)
                return 0;
            /* Level 2: no RC4 */
            if (level >= 2 && c->algorithm_enc == SSL_RC4)
                return 0;
            /* Level 3: forward secure ciphersuites only */
            if (level >= 3 && c->min_tls != TLS1_3_VERSION &&
                               !(c->algorithm_mkey & (SSL_kEDH | SSL_kEECDH)))
                return 0;
            break;
        }
    case SSL_SECOP_VERSION:
        if (!SSL_IS_DTLS(s)) {
            /* SSLv3 not allowed at level 2 */
            if (nid <= SSL3_VERSION && level >= 2)
                return 0;
            /* TLS v1.1 and above only for level 3 */
            if (nid <= TLS1_VERSION && level >= 3)
                return 0;
            /* TLS v1.2 only for level 4 and above */
            if (nid <= TLS1_1_VERSION && level >= 4)
                return 0;
        } else {
            /* DTLS v1.2 only for level 4 and above */
            if (DTLS_VERSION_LT(nid, DTLS1_2_VERSION) && level >= 4)
                return 0;
        }
        break;

    case SSL_SECOP_COMPRESSION:
        if (level >= 2)
            return 0;
        break;
    case SSL_SECOP_TICKET:
        if (level >= 3)
            return 0;
        break;
    default:
        if (bits < minbits)
            return 0;
    }
    return 1;
}

int ssl_security(const SSL *s, int op, int bits, int nid, void *other)
{
    return s->cert->sec_cb(s, NULL, op, bits, nid, other, s->cert->sec_ex);
}

int ssl_ctx_security(const SSL_CTX *ctx, int op, int bits, int nid, void *other)
{
    return ctx->cert->sec_cb(NULL, ctx, op, bits, nid, other,
                             ctx->cert->sec_ex);
}

int ssl_cert_lookup_by_nid(int nid, size_t *pidx)
{
    size_t i;

    for (i = 0; i < OSSL_NELEM(ssl_cert_info); i++) {
        if (ssl_cert_info[i].nid == nid) {
            *pidx = i;
            return 1;
        }
    }

    return 0;
}

const SSL_CERT_LOOKUP *ssl_cert_lookup_by_pkey(const EVP_PKEY *pk, size_t *pidx)
{
    int nid = EVP_PKEY_id(pk);
    size_t tmpidx;

    if (nid == NID_undef)
        return NULL;

    if (!ssl_cert_lookup_by_nid(nid, &tmpidx))
        return NULL;

    if (pidx != NULL)
        *pidx = tmpidx;

    return &ssl_cert_info[tmpidx];
}

const SSL_CERT_LOOKUP *ssl_cert_lookup_by_idx(size_t idx)
{
    if (idx >= OSSL_NELEM(ssl_cert_info))
        return NULL;
    return &ssl_cert_info[idx];
}
