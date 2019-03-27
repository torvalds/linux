/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "ssl_locl.h"
#include "packet_locl.h"
#include <openssl/bio.h>
#include <openssl/objects.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/pem.h>

static int ssl_set_cert(CERT *c, X509 *x509);
static int ssl_set_pkey(CERT *c, EVP_PKEY *pkey);

#define  SYNTHV1CONTEXT     (SSL_EXT_TLS1_2_AND_BELOW_ONLY \
                             | SSL_EXT_CLIENT_HELLO \
                             | SSL_EXT_TLS1_2_SERVER_HELLO \
                             | SSL_EXT_IGNORE_ON_RESUMPTION)

int SSL_use_certificate(SSL *ssl, X509 *x)
{
    int rv;
    if (x == NULL) {
        SSLerr(SSL_F_SSL_USE_CERTIFICATE, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    rv = ssl_security_cert(ssl, NULL, x, 0, 1);
    if (rv != 1) {
        SSLerr(SSL_F_SSL_USE_CERTIFICATE, rv);
        return 0;
    }

    return ssl_set_cert(ssl->cert, x);
}

int SSL_use_certificate_file(SSL *ssl, const char *file, int type)
{
    int j;
    BIO *in;
    int ret = 0;
    X509 *x = NULL;

    in = BIO_new(BIO_s_file());
    if (in == NULL) {
        SSLerr(SSL_F_SSL_USE_CERTIFICATE_FILE, ERR_R_BUF_LIB);
        goto end;
    }

    if (BIO_read_filename(in, file) <= 0) {
        SSLerr(SSL_F_SSL_USE_CERTIFICATE_FILE, ERR_R_SYS_LIB);
        goto end;
    }
    if (type == SSL_FILETYPE_ASN1) {
        j = ERR_R_ASN1_LIB;
        x = d2i_X509_bio(in, NULL);
    } else if (type == SSL_FILETYPE_PEM) {
        j = ERR_R_PEM_LIB;
        x = PEM_read_bio_X509(in, NULL, ssl->default_passwd_callback,
                              ssl->default_passwd_callback_userdata);
    } else {
        SSLerr(SSL_F_SSL_USE_CERTIFICATE_FILE, SSL_R_BAD_SSL_FILETYPE);
        goto end;
    }

    if (x == NULL) {
        SSLerr(SSL_F_SSL_USE_CERTIFICATE_FILE, j);
        goto end;
    }

    ret = SSL_use_certificate(ssl, x);
 end:
    X509_free(x);
    BIO_free(in);
    return ret;
}

int SSL_use_certificate_ASN1(SSL *ssl, const unsigned char *d, int len)
{
    X509 *x;
    int ret;

    x = d2i_X509(NULL, &d, (long)len);
    if (x == NULL) {
        SSLerr(SSL_F_SSL_USE_CERTIFICATE_ASN1, ERR_R_ASN1_LIB);
        return 0;
    }

    ret = SSL_use_certificate(ssl, x);
    X509_free(x);
    return ret;
}

#ifndef OPENSSL_NO_RSA
int SSL_use_RSAPrivateKey(SSL *ssl, RSA *rsa)
{
    EVP_PKEY *pkey;
    int ret;

    if (rsa == NULL) {
        SSLerr(SSL_F_SSL_USE_RSAPRIVATEKEY, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    if ((pkey = EVP_PKEY_new()) == NULL) {
        SSLerr(SSL_F_SSL_USE_RSAPRIVATEKEY, ERR_R_EVP_LIB);
        return 0;
    }

    RSA_up_ref(rsa);
    if (EVP_PKEY_assign_RSA(pkey, rsa) <= 0) {
        RSA_free(rsa);
        EVP_PKEY_free(pkey);
        return 0;
    }

    ret = ssl_set_pkey(ssl->cert, pkey);
    EVP_PKEY_free(pkey);
    return ret;
}
#endif

static int ssl_set_pkey(CERT *c, EVP_PKEY *pkey)
{
    size_t i;

    if (ssl_cert_lookup_by_pkey(pkey, &i) == NULL) {
        SSLerr(SSL_F_SSL_SET_PKEY, SSL_R_UNKNOWN_CERTIFICATE_TYPE);
        return 0;
    }

    if (c->pkeys[i].x509 != NULL) {
        EVP_PKEY *pktmp;
        pktmp = X509_get0_pubkey(c->pkeys[i].x509);
        if (pktmp == NULL) {
            SSLerr(SSL_F_SSL_SET_PKEY, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        /*
         * The return code from EVP_PKEY_copy_parameters is deliberately
         * ignored. Some EVP_PKEY types cannot do this.
         */
        EVP_PKEY_copy_parameters(pktmp, pkey);
        ERR_clear_error();

#ifndef OPENSSL_NO_RSA
        /*
         * Don't check the public/private key, this is mostly for smart
         * cards.
         */
        if (EVP_PKEY_id(pkey) == EVP_PKEY_RSA
            && RSA_flags(EVP_PKEY_get0_RSA(pkey)) & RSA_METHOD_FLAG_NO_CHECK) ;
        else
#endif
        if (!X509_check_private_key(c->pkeys[i].x509, pkey)) {
            X509_free(c->pkeys[i].x509);
            c->pkeys[i].x509 = NULL;
            return 0;
        }
    }

    EVP_PKEY_free(c->pkeys[i].privatekey);
    EVP_PKEY_up_ref(pkey);
    c->pkeys[i].privatekey = pkey;
    c->key = &c->pkeys[i];
    return 1;
}

#ifndef OPENSSL_NO_RSA
int SSL_use_RSAPrivateKey_file(SSL *ssl, const char *file, int type)
{
    int j, ret = 0;
    BIO *in;
    RSA *rsa = NULL;

    in = BIO_new(BIO_s_file());
    if (in == NULL) {
        SSLerr(SSL_F_SSL_USE_RSAPRIVATEKEY_FILE, ERR_R_BUF_LIB);
        goto end;
    }

    if (BIO_read_filename(in, file) <= 0) {
        SSLerr(SSL_F_SSL_USE_RSAPRIVATEKEY_FILE, ERR_R_SYS_LIB);
        goto end;
    }
    if (type == SSL_FILETYPE_ASN1) {
        j = ERR_R_ASN1_LIB;
        rsa = d2i_RSAPrivateKey_bio(in, NULL);
    } else if (type == SSL_FILETYPE_PEM) {
        j = ERR_R_PEM_LIB;
        rsa = PEM_read_bio_RSAPrivateKey(in, NULL,
                                         ssl->default_passwd_callback,
                                         ssl->default_passwd_callback_userdata);
    } else {
        SSLerr(SSL_F_SSL_USE_RSAPRIVATEKEY_FILE, SSL_R_BAD_SSL_FILETYPE);
        goto end;
    }
    if (rsa == NULL) {
        SSLerr(SSL_F_SSL_USE_RSAPRIVATEKEY_FILE, j);
        goto end;
    }
    ret = SSL_use_RSAPrivateKey(ssl, rsa);
    RSA_free(rsa);
 end:
    BIO_free(in);
    return ret;
}

int SSL_use_RSAPrivateKey_ASN1(SSL *ssl, const unsigned char *d, long len)
{
    int ret;
    const unsigned char *p;
    RSA *rsa;

    p = d;
    if ((rsa = d2i_RSAPrivateKey(NULL, &p, (long)len)) == NULL) {
        SSLerr(SSL_F_SSL_USE_RSAPRIVATEKEY_ASN1, ERR_R_ASN1_LIB);
        return 0;
    }

    ret = SSL_use_RSAPrivateKey(ssl, rsa);
    RSA_free(rsa);
    return ret;
}
#endif                          /* !OPENSSL_NO_RSA */

int SSL_use_PrivateKey(SSL *ssl, EVP_PKEY *pkey)
{
    int ret;

    if (pkey == NULL) {
        SSLerr(SSL_F_SSL_USE_PRIVATEKEY, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    ret = ssl_set_pkey(ssl->cert, pkey);
    return ret;
}

int SSL_use_PrivateKey_file(SSL *ssl, const char *file, int type)
{
    int j, ret = 0;
    BIO *in;
    EVP_PKEY *pkey = NULL;

    in = BIO_new(BIO_s_file());
    if (in == NULL) {
        SSLerr(SSL_F_SSL_USE_PRIVATEKEY_FILE, ERR_R_BUF_LIB);
        goto end;
    }

    if (BIO_read_filename(in, file) <= 0) {
        SSLerr(SSL_F_SSL_USE_PRIVATEKEY_FILE, ERR_R_SYS_LIB);
        goto end;
    }
    if (type == SSL_FILETYPE_PEM) {
        j = ERR_R_PEM_LIB;
        pkey = PEM_read_bio_PrivateKey(in, NULL,
                                       ssl->default_passwd_callback,
                                       ssl->default_passwd_callback_userdata);
    } else if (type == SSL_FILETYPE_ASN1) {
        j = ERR_R_ASN1_LIB;
        pkey = d2i_PrivateKey_bio(in, NULL);
    } else {
        SSLerr(SSL_F_SSL_USE_PRIVATEKEY_FILE, SSL_R_BAD_SSL_FILETYPE);
        goto end;
    }
    if (pkey == NULL) {
        SSLerr(SSL_F_SSL_USE_PRIVATEKEY_FILE, j);
        goto end;
    }
    ret = SSL_use_PrivateKey(ssl, pkey);
    EVP_PKEY_free(pkey);
 end:
    BIO_free(in);
    return ret;
}

int SSL_use_PrivateKey_ASN1(int type, SSL *ssl, const unsigned char *d,
                            long len)
{
    int ret;
    const unsigned char *p;
    EVP_PKEY *pkey;

    p = d;
    if ((pkey = d2i_PrivateKey(type, NULL, &p, (long)len)) == NULL) {
        SSLerr(SSL_F_SSL_USE_PRIVATEKEY_ASN1, ERR_R_ASN1_LIB);
        return 0;
    }

    ret = SSL_use_PrivateKey(ssl, pkey);
    EVP_PKEY_free(pkey);
    return ret;
}

int SSL_CTX_use_certificate(SSL_CTX *ctx, X509 *x)
{
    int rv;
    if (x == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_CERTIFICATE, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    rv = ssl_security_cert(NULL, ctx, x, 0, 1);
    if (rv != 1) {
        SSLerr(SSL_F_SSL_CTX_USE_CERTIFICATE, rv);
        return 0;
    }
    return ssl_set_cert(ctx->cert, x);
}

static int ssl_set_cert(CERT *c, X509 *x)
{
    EVP_PKEY *pkey;
    size_t i;

    pkey = X509_get0_pubkey(x);
    if (pkey == NULL) {
        SSLerr(SSL_F_SSL_SET_CERT, SSL_R_X509_LIB);
        return 0;
    }

    if (ssl_cert_lookup_by_pkey(pkey, &i) == NULL) {
        SSLerr(SSL_F_SSL_SET_CERT, SSL_R_UNKNOWN_CERTIFICATE_TYPE);
        return 0;
    }
#ifndef OPENSSL_NO_EC
    if (i == SSL_PKEY_ECC && !EC_KEY_can_sign(EVP_PKEY_get0_EC_KEY(pkey))) {
        SSLerr(SSL_F_SSL_SET_CERT, SSL_R_ECC_CERT_NOT_FOR_SIGNING);
        return 0;
    }
#endif
    if (c->pkeys[i].privatekey != NULL) {
        /*
         * The return code from EVP_PKEY_copy_parameters is deliberately
         * ignored. Some EVP_PKEY types cannot do this.
         */
        EVP_PKEY_copy_parameters(pkey, c->pkeys[i].privatekey);
        ERR_clear_error();

#ifndef OPENSSL_NO_RSA
        /*
         * Don't check the public/private key, this is mostly for smart
         * cards.
         */
        if (EVP_PKEY_id(c->pkeys[i].privatekey) == EVP_PKEY_RSA
            && RSA_flags(EVP_PKEY_get0_RSA(c->pkeys[i].privatekey)) &
            RSA_METHOD_FLAG_NO_CHECK) ;
        else
#endif                          /* OPENSSL_NO_RSA */
        if (!X509_check_private_key(x, c->pkeys[i].privatekey)) {
            /*
             * don't fail for a cert/key mismatch, just free current private
             * key (when switching to a different cert & key, first this
             * function should be used, then ssl_set_pkey
             */
            EVP_PKEY_free(c->pkeys[i].privatekey);
            c->pkeys[i].privatekey = NULL;
            /* clear error queue */
            ERR_clear_error();
        }
    }

    X509_free(c->pkeys[i].x509);
    X509_up_ref(x);
    c->pkeys[i].x509 = x;
    c->key = &(c->pkeys[i]);

    return 1;
}

int SSL_CTX_use_certificate_file(SSL_CTX *ctx, const char *file, int type)
{
    int j;
    BIO *in;
    int ret = 0;
    X509 *x = NULL;

    in = BIO_new(BIO_s_file());
    if (in == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_CERTIFICATE_FILE, ERR_R_BUF_LIB);
        goto end;
    }

    if (BIO_read_filename(in, file) <= 0) {
        SSLerr(SSL_F_SSL_CTX_USE_CERTIFICATE_FILE, ERR_R_SYS_LIB);
        goto end;
    }
    if (type == SSL_FILETYPE_ASN1) {
        j = ERR_R_ASN1_LIB;
        x = d2i_X509_bio(in, NULL);
    } else if (type == SSL_FILETYPE_PEM) {
        j = ERR_R_PEM_LIB;
        x = PEM_read_bio_X509(in, NULL, ctx->default_passwd_callback,
                              ctx->default_passwd_callback_userdata);
    } else {
        SSLerr(SSL_F_SSL_CTX_USE_CERTIFICATE_FILE, SSL_R_BAD_SSL_FILETYPE);
        goto end;
    }

    if (x == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_CERTIFICATE_FILE, j);
        goto end;
    }

    ret = SSL_CTX_use_certificate(ctx, x);
 end:
    X509_free(x);
    BIO_free(in);
    return ret;
}

int SSL_CTX_use_certificate_ASN1(SSL_CTX *ctx, int len, const unsigned char *d)
{
    X509 *x;
    int ret;

    x = d2i_X509(NULL, &d, (long)len);
    if (x == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_CERTIFICATE_ASN1, ERR_R_ASN1_LIB);
        return 0;
    }

    ret = SSL_CTX_use_certificate(ctx, x);
    X509_free(x);
    return ret;
}

#ifndef OPENSSL_NO_RSA
int SSL_CTX_use_RSAPrivateKey(SSL_CTX *ctx, RSA *rsa)
{
    int ret;
    EVP_PKEY *pkey;

    if (rsa == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_RSAPRIVATEKEY, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    if ((pkey = EVP_PKEY_new()) == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_RSAPRIVATEKEY, ERR_R_EVP_LIB);
        return 0;
    }

    RSA_up_ref(rsa);
    if (EVP_PKEY_assign_RSA(pkey, rsa) <= 0) {
        RSA_free(rsa);
        EVP_PKEY_free(pkey);
        return 0;
    }

    ret = ssl_set_pkey(ctx->cert, pkey);
    EVP_PKEY_free(pkey);
    return ret;
}

int SSL_CTX_use_RSAPrivateKey_file(SSL_CTX *ctx, const char *file, int type)
{
    int j, ret = 0;
    BIO *in;
    RSA *rsa = NULL;

    in = BIO_new(BIO_s_file());
    if (in == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_RSAPRIVATEKEY_FILE, ERR_R_BUF_LIB);
        goto end;
    }

    if (BIO_read_filename(in, file) <= 0) {
        SSLerr(SSL_F_SSL_CTX_USE_RSAPRIVATEKEY_FILE, ERR_R_SYS_LIB);
        goto end;
    }
    if (type == SSL_FILETYPE_ASN1) {
        j = ERR_R_ASN1_LIB;
        rsa = d2i_RSAPrivateKey_bio(in, NULL);
    } else if (type == SSL_FILETYPE_PEM) {
        j = ERR_R_PEM_LIB;
        rsa = PEM_read_bio_RSAPrivateKey(in, NULL,
                                         ctx->default_passwd_callback,
                                         ctx->default_passwd_callback_userdata);
    } else {
        SSLerr(SSL_F_SSL_CTX_USE_RSAPRIVATEKEY_FILE, SSL_R_BAD_SSL_FILETYPE);
        goto end;
    }
    if (rsa == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_RSAPRIVATEKEY_FILE, j);
        goto end;
    }
    ret = SSL_CTX_use_RSAPrivateKey(ctx, rsa);
    RSA_free(rsa);
 end:
    BIO_free(in);
    return ret;
}

int SSL_CTX_use_RSAPrivateKey_ASN1(SSL_CTX *ctx, const unsigned char *d,
                                   long len)
{
    int ret;
    const unsigned char *p;
    RSA *rsa;

    p = d;
    if ((rsa = d2i_RSAPrivateKey(NULL, &p, (long)len)) == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_RSAPRIVATEKEY_ASN1, ERR_R_ASN1_LIB);
        return 0;
    }

    ret = SSL_CTX_use_RSAPrivateKey(ctx, rsa);
    RSA_free(rsa);
    return ret;
}
#endif                          /* !OPENSSL_NO_RSA */

int SSL_CTX_use_PrivateKey(SSL_CTX *ctx, EVP_PKEY *pkey)
{
    if (pkey == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_PRIVATEKEY, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    return ssl_set_pkey(ctx->cert, pkey);
}

int SSL_CTX_use_PrivateKey_file(SSL_CTX *ctx, const char *file, int type)
{
    int j, ret = 0;
    BIO *in;
    EVP_PKEY *pkey = NULL;

    in = BIO_new(BIO_s_file());
    if (in == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_PRIVATEKEY_FILE, ERR_R_BUF_LIB);
        goto end;
    }

    if (BIO_read_filename(in, file) <= 0) {
        SSLerr(SSL_F_SSL_CTX_USE_PRIVATEKEY_FILE, ERR_R_SYS_LIB);
        goto end;
    }
    if (type == SSL_FILETYPE_PEM) {
        j = ERR_R_PEM_LIB;
        pkey = PEM_read_bio_PrivateKey(in, NULL,
                                       ctx->default_passwd_callback,
                                       ctx->default_passwd_callback_userdata);
    } else if (type == SSL_FILETYPE_ASN1) {
        j = ERR_R_ASN1_LIB;
        pkey = d2i_PrivateKey_bio(in, NULL);
    } else {
        SSLerr(SSL_F_SSL_CTX_USE_PRIVATEKEY_FILE, SSL_R_BAD_SSL_FILETYPE);
        goto end;
    }
    if (pkey == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_PRIVATEKEY_FILE, j);
        goto end;
    }
    ret = SSL_CTX_use_PrivateKey(ctx, pkey);
    EVP_PKEY_free(pkey);
 end:
    BIO_free(in);
    return ret;
}

int SSL_CTX_use_PrivateKey_ASN1(int type, SSL_CTX *ctx,
                                const unsigned char *d, long len)
{
    int ret;
    const unsigned char *p;
    EVP_PKEY *pkey;

    p = d;
    if ((pkey = d2i_PrivateKey(type, NULL, &p, (long)len)) == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_PRIVATEKEY_ASN1, ERR_R_ASN1_LIB);
        return 0;
    }

    ret = SSL_CTX_use_PrivateKey(ctx, pkey);
    EVP_PKEY_free(pkey);
    return ret;
}

/*
 * Read a file that contains our certificate in "PEM" format, possibly
 * followed by a sequence of CA certificates that should be sent to the peer
 * in the Certificate message.
 */
static int use_certificate_chain_file(SSL_CTX *ctx, SSL *ssl, const char *file)
{
    BIO *in;
    int ret = 0;
    X509 *x = NULL;
    pem_password_cb *passwd_callback;
    void *passwd_callback_userdata;

    ERR_clear_error();          /* clear error stack for
                                 * SSL_CTX_use_certificate() */

    if (ctx != NULL) {
        passwd_callback = ctx->default_passwd_callback;
        passwd_callback_userdata = ctx->default_passwd_callback_userdata;
    } else {
        passwd_callback = ssl->default_passwd_callback;
        passwd_callback_userdata = ssl->default_passwd_callback_userdata;
    }

    in = BIO_new(BIO_s_file());
    if (in == NULL) {
        SSLerr(SSL_F_USE_CERTIFICATE_CHAIN_FILE, ERR_R_BUF_LIB);
        goto end;
    }

    if (BIO_read_filename(in, file) <= 0) {
        SSLerr(SSL_F_USE_CERTIFICATE_CHAIN_FILE, ERR_R_SYS_LIB);
        goto end;
    }

    x = PEM_read_bio_X509_AUX(in, NULL, passwd_callback,
                              passwd_callback_userdata);
    if (x == NULL) {
        SSLerr(SSL_F_USE_CERTIFICATE_CHAIN_FILE, ERR_R_PEM_LIB);
        goto end;
    }

    if (ctx)
        ret = SSL_CTX_use_certificate(ctx, x);
    else
        ret = SSL_use_certificate(ssl, x);

    if (ERR_peek_error() != 0)
        ret = 0;                /* Key/certificate mismatch doesn't imply
                                 * ret==0 ... */
    if (ret) {
        /*
         * If we could set up our certificate, now proceed to the CA
         * certificates.
         */
        X509 *ca;
        int r;
        unsigned long err;

        if (ctx)
            r = SSL_CTX_clear_chain_certs(ctx);
        else
            r = SSL_clear_chain_certs(ssl);

        if (r == 0) {
            ret = 0;
            goto end;
        }

        while ((ca = PEM_read_bio_X509(in, NULL, passwd_callback,
                                       passwd_callback_userdata))
               != NULL) {
            if (ctx)
                r = SSL_CTX_add0_chain_cert(ctx, ca);
            else
                r = SSL_add0_chain_cert(ssl, ca);
            /*
             * Note that we must not free ca if it was successfully added to
             * the chain (while we must free the main certificate, since its
             * reference count is increased by SSL_CTX_use_certificate).
             */
            if (!r) {
                X509_free(ca);
                ret = 0;
                goto end;
            }
        }
        /* When the while loop ends, it's usually just EOF. */
        err = ERR_peek_last_error();
        if (ERR_GET_LIB(err) == ERR_LIB_PEM
            && ERR_GET_REASON(err) == PEM_R_NO_START_LINE)
            ERR_clear_error();
        else
            ret = 0;            /* some real error */
    }

 end:
    X509_free(x);
    BIO_free(in);
    return ret;
}

int SSL_CTX_use_certificate_chain_file(SSL_CTX *ctx, const char *file)
{
    return use_certificate_chain_file(ctx, NULL, file);
}

int SSL_use_certificate_chain_file(SSL *ssl, const char *file)
{
    return use_certificate_chain_file(NULL, ssl, file);
}

static int serverinfo_find_extension(const unsigned char *serverinfo,
                                     size_t serverinfo_length,
                                     unsigned int extension_type,
                                     const unsigned char **extension_data,
                                     size_t *extension_length)
{
    PACKET pkt, data;

    *extension_data = NULL;
    *extension_length = 0;
    if (serverinfo == NULL || serverinfo_length == 0)
        return -1;

    if (!PACKET_buf_init(&pkt, serverinfo, serverinfo_length))
        return -1;

    for (;;) {
        unsigned int type = 0;
        unsigned long context = 0;

        /* end of serverinfo */
        if (PACKET_remaining(&pkt) == 0)
            return 0;           /* Extension not found */

        if (!PACKET_get_net_4(&pkt, &context)
                || !PACKET_get_net_2(&pkt, &type)
                || !PACKET_get_length_prefixed_2(&pkt, &data))
            return -1;

        if (type == extension_type) {
            *extension_data = PACKET_data(&data);
            *extension_length = PACKET_remaining(&data);;
            return 1;           /* Success */
        }
    }
    /* Unreachable */
}

static int serverinfoex_srv_parse_cb(SSL *s, unsigned int ext_type,
                                     unsigned int context,
                                     const unsigned char *in,
                                     size_t inlen, X509 *x, size_t chainidx,
                                     int *al, void *arg)
{

    if (inlen != 0) {
        *al = SSL_AD_DECODE_ERROR;
        return 0;
    }

    return 1;
}

static int serverinfo_srv_parse_cb(SSL *s, unsigned int ext_type,
                                   const unsigned char *in,
                                   size_t inlen, int *al, void *arg)
{
    return serverinfoex_srv_parse_cb(s, ext_type, 0, in, inlen, NULL, 0, al,
                                     arg);
}

static int serverinfoex_srv_add_cb(SSL *s, unsigned int ext_type,
                                   unsigned int context,
                                   const unsigned char **out,
                                   size_t *outlen, X509 *x, size_t chainidx,
                                   int *al, void *arg)
{
    const unsigned char *serverinfo = NULL;
    size_t serverinfo_length = 0;

    /* We only support extensions for the first Certificate */
    if ((context & SSL_EXT_TLS1_3_CERTIFICATE) != 0 && chainidx > 0)
        return 0;

    /* Is there serverinfo data for the chosen server cert? */
    if ((ssl_get_server_cert_serverinfo(s, &serverinfo,
                                        &serverinfo_length)) != 0) {
        /* Find the relevant extension from the serverinfo */
        int retval = serverinfo_find_extension(serverinfo, serverinfo_length,
                                               ext_type, out, outlen);
        if (retval == -1) {
            *al = SSL_AD_INTERNAL_ERROR;
            return -1;          /* Error */
        }
        if (retval == 0)
            return 0;           /* No extension found, don't send extension */
        return 1;               /* Send extension */
    }
    return 0;                   /* No serverinfo data found, don't send
                                 * extension */
}

static int serverinfo_srv_add_cb(SSL *s, unsigned int ext_type,
                                 const unsigned char **out, size_t *outlen,
                                 int *al, void *arg)
{
    return serverinfoex_srv_add_cb(s, ext_type, 0, out, outlen, NULL, 0, al,
                                   arg);
}

/*
 * With a NULL context, this function just checks that the serverinfo data
 * parses correctly.  With a non-NULL context, it registers callbacks for
 * the included extensions.
 */
static int serverinfo_process_buffer(unsigned int version,
                                     const unsigned char *serverinfo,
                                     size_t serverinfo_length, SSL_CTX *ctx)
{
    PACKET pkt;

    if (serverinfo == NULL || serverinfo_length == 0)
        return 0;

    if (version != SSL_SERVERINFOV1 && version != SSL_SERVERINFOV2)
        return 0;

    if (!PACKET_buf_init(&pkt, serverinfo, serverinfo_length))
        return 0;

    while (PACKET_remaining(&pkt)) {
        unsigned long context = 0;
        unsigned int ext_type = 0;
        PACKET data;

        if ((version == SSL_SERVERINFOV2 && !PACKET_get_net_4(&pkt, &context))
                || !PACKET_get_net_2(&pkt, &ext_type)
                || !PACKET_get_length_prefixed_2(&pkt, &data))
            return 0;

        if (ctx == NULL)
            continue;

        /*
         * The old style custom extensions API could be set separately for
         * server/client, i.e. you could set one custom extension for a client,
         * and *for the same extension in the same SSL_CTX* you could set a
         * custom extension for the server as well. It seems quite weird to be
         * setting a custom extension for both client and server in a single
         * SSL_CTX - but theoretically possible. This isn't possible in the
         * new API. Therefore, if we have V1 serverinfo we use the old API. We
         * also use the old API even if we have V2 serverinfo but the context
         * looks like an old style <= TLSv1.2 extension.
         */
        if (version == SSL_SERVERINFOV1 || context == SYNTHV1CONTEXT) {
            if (!SSL_CTX_add_server_custom_ext(ctx, ext_type,
                                               serverinfo_srv_add_cb,
                                               NULL, NULL,
                                               serverinfo_srv_parse_cb,
                                               NULL))
                return 0;
        } else {
            if (!SSL_CTX_add_custom_ext(ctx, ext_type, context,
                                        serverinfoex_srv_add_cb,
                                        NULL, NULL,
                                        serverinfoex_srv_parse_cb,
                                        NULL))
                return 0;
        }
    }

    return 1;
}

int SSL_CTX_use_serverinfo_ex(SSL_CTX *ctx, unsigned int version,
                              const unsigned char *serverinfo,
                              size_t serverinfo_length)
{
    unsigned char *new_serverinfo;

    if (ctx == NULL || serverinfo == NULL || serverinfo_length == 0) {
        SSLerr(SSL_F_SSL_CTX_USE_SERVERINFO_EX, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    if (!serverinfo_process_buffer(version, serverinfo, serverinfo_length,
                                   NULL)) {
        SSLerr(SSL_F_SSL_CTX_USE_SERVERINFO_EX, SSL_R_INVALID_SERVERINFO_DATA);
        return 0;
    }
    if (ctx->cert->key == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_SERVERINFO_EX, ERR_R_INTERNAL_ERROR);
        return 0;
    }
    new_serverinfo = OPENSSL_realloc(ctx->cert->key->serverinfo,
                                     serverinfo_length);
    if (new_serverinfo == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_SERVERINFO_EX, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    ctx->cert->key->serverinfo = new_serverinfo;
    memcpy(ctx->cert->key->serverinfo, serverinfo, serverinfo_length);
    ctx->cert->key->serverinfo_length = serverinfo_length;

    /*
     * Now that the serverinfo is validated and stored, go ahead and
     * register callbacks.
     */
    if (!serverinfo_process_buffer(version, serverinfo, serverinfo_length,
                                   ctx)) {
        SSLerr(SSL_F_SSL_CTX_USE_SERVERINFO_EX, SSL_R_INVALID_SERVERINFO_DATA);
        return 0;
    }
    return 1;
}

int SSL_CTX_use_serverinfo(SSL_CTX *ctx, const unsigned char *serverinfo,
                           size_t serverinfo_length)
{
    return SSL_CTX_use_serverinfo_ex(ctx, SSL_SERVERINFOV1, serverinfo,
                                     serverinfo_length);
}

int SSL_CTX_use_serverinfo_file(SSL_CTX *ctx, const char *file)
{
    unsigned char *serverinfo = NULL;
    unsigned char *tmp;
    size_t serverinfo_length = 0;
    unsigned char *extension = 0;
    long extension_length = 0;
    char *name = NULL;
    char *header = NULL;
    char namePrefix1[] = "SERVERINFO FOR ";
    char namePrefix2[] = "SERVERINFOV2 FOR ";
    int ret = 0;
    BIO *bin = NULL;
    size_t num_extensions = 0, contextoff = 0;

    if (ctx == NULL || file == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_SERVERINFO_FILE, ERR_R_PASSED_NULL_PARAMETER);
        goto end;
    }

    bin = BIO_new(BIO_s_file());
    if (bin == NULL) {
        SSLerr(SSL_F_SSL_CTX_USE_SERVERINFO_FILE, ERR_R_BUF_LIB);
        goto end;
    }
    if (BIO_read_filename(bin, file) <= 0) {
        SSLerr(SSL_F_SSL_CTX_USE_SERVERINFO_FILE, ERR_R_SYS_LIB);
        goto end;
    }

    for (num_extensions = 0;; num_extensions++) {
        unsigned int version;

        if (PEM_read_bio(bin, &name, &header, &extension, &extension_length)
            == 0) {
            /*
             * There must be at least one extension in this file
             */
            if (num_extensions == 0) {
                SSLerr(SSL_F_SSL_CTX_USE_SERVERINFO_FILE,
                       SSL_R_NO_PEM_EXTENSIONS);
                goto end;
            } else              /* End of file, we're done */
                break;
        }
        /* Check that PEM name starts with "BEGIN SERVERINFO FOR " */
        if (strlen(name) < strlen(namePrefix1)) {
            SSLerr(SSL_F_SSL_CTX_USE_SERVERINFO_FILE, SSL_R_PEM_NAME_TOO_SHORT);
            goto end;
        }
        if (strncmp(name, namePrefix1, strlen(namePrefix1)) == 0) {
            version = SSL_SERVERINFOV1;
        } else {
            if (strlen(name) < strlen(namePrefix2)) {
                SSLerr(SSL_F_SSL_CTX_USE_SERVERINFO_FILE,
                       SSL_R_PEM_NAME_TOO_SHORT);
                goto end;
            }
            if (strncmp(name, namePrefix2, strlen(namePrefix2)) != 0) {
                SSLerr(SSL_F_SSL_CTX_USE_SERVERINFO_FILE,
                       SSL_R_PEM_NAME_BAD_PREFIX);
                goto end;
            }
            version = SSL_SERVERINFOV2;
        }
        /*
         * Check that the decoded PEM data is plausible (valid length field)
         */
        if (version == SSL_SERVERINFOV1) {
            /* 4 byte header: 2 bytes type, 2 bytes len */
            if (extension_length < 4
                    || (extension[2] << 8) + extension[3]
                       != extension_length - 4) {
                SSLerr(SSL_F_SSL_CTX_USE_SERVERINFO_FILE, SSL_R_BAD_DATA);
                goto end;
            }
            /*
             * File does not have a context value so we must take account of
             * this later.
             */
            contextoff = 4;
        } else {
            /* 8 byte header: 4 bytes context, 2 bytes type, 2 bytes len */
            if (extension_length < 8
                    || (extension[6] << 8) + extension[7]
                       != extension_length - 8) {
                SSLerr(SSL_F_SSL_CTX_USE_SERVERINFO_FILE, SSL_R_BAD_DATA);
                goto end;
            }
        }
        /* Append the decoded extension to the serverinfo buffer */
        tmp = OPENSSL_realloc(serverinfo, serverinfo_length + extension_length
                                          + contextoff);
        if (tmp == NULL) {
            SSLerr(SSL_F_SSL_CTX_USE_SERVERINFO_FILE, ERR_R_MALLOC_FAILURE);
            goto end;
        }
        serverinfo = tmp;
        if (contextoff > 0) {
            unsigned char *sinfo = serverinfo + serverinfo_length;

            /* We know this only uses the last 2 bytes */
            sinfo[0] = 0;
            sinfo[1] = 0;
            sinfo[2] = (SYNTHV1CONTEXT >> 8) & 0xff;
            sinfo[3] = SYNTHV1CONTEXT & 0xff;
        }
        memcpy(serverinfo + serverinfo_length + contextoff,
               extension, extension_length);
        serverinfo_length += extension_length + contextoff;

        OPENSSL_free(name);
        name = NULL;
        OPENSSL_free(header);
        header = NULL;
        OPENSSL_free(extension);
        extension = NULL;
    }

    ret = SSL_CTX_use_serverinfo_ex(ctx, SSL_SERVERINFOV2, serverinfo,
                                    serverinfo_length);
 end:
    /* SSL_CTX_use_serverinfo makes a local copy of the serverinfo. */
    OPENSSL_free(name);
    OPENSSL_free(header);
    OPENSSL_free(extension);
    OPENSSL_free(serverinfo);
    BIO_free(bin);
    return ret;
}

static int ssl_set_cert_and_key(SSL *ssl, SSL_CTX *ctx, X509 *x509, EVP_PKEY *privatekey,
                                STACK_OF(X509) *chain, int override)
{
    int ret = 0;
    size_t i;
    int j;
    int rv;
    CERT *c = ssl != NULL ? ssl->cert : ctx->cert;
    STACK_OF(X509) *dup_chain = NULL;
    EVP_PKEY *pubkey = NULL;

    /* Do all security checks before anything else */
    rv = ssl_security_cert(ssl, ctx, x509, 0, 1);
    if (rv != 1) {
        SSLerr(SSL_F_SSL_SET_CERT_AND_KEY, rv);
        goto out;
    }
    for (j = 0; j < sk_X509_num(chain); j++) {
        rv = ssl_security_cert(ssl, ctx, sk_X509_value(chain, j), 0, 0);
        if (rv != 1) {
            SSLerr(SSL_F_SSL_SET_CERT_AND_KEY, rv);
            goto out;
        }
    }

    pubkey = X509_get_pubkey(x509); /* bumps reference */
    if (pubkey == NULL)
        goto out;
    if (privatekey == NULL) {
        privatekey = pubkey;
    } else {
        /* For RSA, which has no parameters, missing returns 0 */
        if (EVP_PKEY_missing_parameters(privatekey)) {
            if (EVP_PKEY_missing_parameters(pubkey)) {
                /* nobody has parameters? - error */
                SSLerr(SSL_F_SSL_SET_CERT_AND_KEY, SSL_R_MISSING_PARAMETERS);
                goto out;
            } else {
                /* copy to privatekey from pubkey */
                EVP_PKEY_copy_parameters(privatekey, pubkey);
            }
        } else if (EVP_PKEY_missing_parameters(pubkey)) {
            /* copy to pubkey from privatekey */
            EVP_PKEY_copy_parameters(pubkey, privatekey);
        } /* else both have parameters */

        /* Copied from ssl_set_cert/pkey */
#ifndef OPENSSL_NO_RSA
        if ((EVP_PKEY_id(privatekey) == EVP_PKEY_RSA) &&
            ((RSA_flags(EVP_PKEY_get0_RSA(privatekey)) & RSA_METHOD_FLAG_NO_CHECK)))
            /* no-op */ ;
        else
#endif
        /* check that key <-> cert match */
        if (EVP_PKEY_cmp(pubkey, privatekey) != 1) {
            SSLerr(SSL_F_SSL_SET_CERT_AND_KEY, SSL_R_PRIVATE_KEY_MISMATCH);
            goto out;
        }
    }
    if (ssl_cert_lookup_by_pkey(pubkey, &i) == NULL) {
        SSLerr(SSL_F_SSL_SET_CERT_AND_KEY, SSL_R_UNKNOWN_CERTIFICATE_TYPE);
        goto out;
    }

    if (!override && (c->pkeys[i].x509 != NULL
                      || c->pkeys[i].privatekey != NULL
                      || c->pkeys[i].chain != NULL)) {
        /* No override, and something already there */
        SSLerr(SSL_F_SSL_SET_CERT_AND_KEY, SSL_R_NOT_REPLACING_CERTIFICATE);
        goto out;
    }

    if (chain != NULL) {
        dup_chain = X509_chain_up_ref(chain);
        if  (dup_chain == NULL) {
            SSLerr(SSL_F_SSL_SET_CERT_AND_KEY, ERR_R_MALLOC_FAILURE);
            goto out;
        }
    }

    sk_X509_pop_free(c->pkeys[i].chain, X509_free);
    c->pkeys[i].chain = dup_chain;

    X509_free(c->pkeys[i].x509);
    X509_up_ref(x509);
    c->pkeys[i].x509 = x509;

    EVP_PKEY_free(c->pkeys[i].privatekey);
    EVP_PKEY_up_ref(privatekey);
    c->pkeys[i].privatekey = privatekey;

    c->key = &(c->pkeys[i]);

    ret = 1;
 out:
    EVP_PKEY_free(pubkey);
    return ret;
}

int SSL_use_cert_and_key(SSL *ssl, X509 *x509, EVP_PKEY *privatekey,
                         STACK_OF(X509) *chain, int override)
{
    return ssl_set_cert_and_key(ssl, NULL, x509, privatekey, chain, override);
}

int SSL_CTX_use_cert_and_key(SSL_CTX *ctx, X509 *x509, EVP_PKEY *privatekey,
                             STACK_OF(X509) *chain, int override)
{
    return ssl_set_cert_and_key(NULL, ctx, x509, privatekey, chain, override);
}
