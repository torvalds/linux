/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <string.h>
#include "rsa_locl.h"
#include <openssl/err.h>

RSA_METHOD *RSA_meth_new(const char *name, int flags)
{
    RSA_METHOD *meth = OPENSSL_zalloc(sizeof(*meth));

    if (meth != NULL) {
        meth->flags = flags;

        meth->name = OPENSSL_strdup(name);
        if (meth->name != NULL)
            return meth;

        OPENSSL_free(meth);
    }

    RSAerr(RSA_F_RSA_METH_NEW, ERR_R_MALLOC_FAILURE);
    return NULL;
}

void RSA_meth_free(RSA_METHOD *meth)
{
    if (meth != NULL) {
        OPENSSL_free(meth->name);
        OPENSSL_free(meth);
    }
}

RSA_METHOD *RSA_meth_dup(const RSA_METHOD *meth)
{
    RSA_METHOD *ret = OPENSSL_malloc(sizeof(*ret));

    if (ret != NULL) {
        memcpy(ret, meth, sizeof(*meth));

        ret->name = OPENSSL_strdup(meth->name);
        if (ret->name != NULL)
            return ret;

        OPENSSL_free(ret);
    }

    RSAerr(RSA_F_RSA_METH_DUP, ERR_R_MALLOC_FAILURE);
    return NULL;
}

const char *RSA_meth_get0_name(const RSA_METHOD *meth)
{
    return meth->name;
}

int RSA_meth_set1_name(RSA_METHOD *meth, const char *name)
{
    char *tmpname = OPENSSL_strdup(name);

    if (tmpname == NULL) {
        RSAerr(RSA_F_RSA_METH_SET1_NAME, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    OPENSSL_free(meth->name);
    meth->name = tmpname;

    return 1;
}

int RSA_meth_get_flags(const RSA_METHOD *meth)
{
    return meth->flags;
}

int RSA_meth_set_flags(RSA_METHOD *meth, int flags)
{
    meth->flags = flags;
    return 1;
}

void *RSA_meth_get0_app_data(const RSA_METHOD *meth)
{
    return meth->app_data;
}

int RSA_meth_set0_app_data(RSA_METHOD *meth, void *app_data)
{
    meth->app_data = app_data;
    return 1;
}

int (*RSA_meth_get_pub_enc(const RSA_METHOD *meth))
    (int flen, const unsigned char *from,
     unsigned char *to, RSA *rsa, int padding)
{
    return meth->rsa_pub_enc;
}

int RSA_meth_set_pub_enc(RSA_METHOD *meth,
                         int (*pub_enc) (int flen, const unsigned char *from,
                                         unsigned char *to, RSA *rsa,
                                         int padding))
{
    meth->rsa_pub_enc = pub_enc;
    return 1;
}

int (*RSA_meth_get_pub_dec(const RSA_METHOD *meth))
    (int flen, const unsigned char *from,
     unsigned char *to, RSA *rsa, int padding)
{
    return meth->rsa_pub_dec;
}

int RSA_meth_set_pub_dec(RSA_METHOD *meth,
                         int (*pub_dec) (int flen, const unsigned char *from,
                                         unsigned char *to, RSA *rsa,
                                         int padding))
{
    meth->rsa_pub_dec = pub_dec;
    return 1;
}

int (*RSA_meth_get_priv_enc(const RSA_METHOD *meth))
    (int flen, const unsigned char *from,
     unsigned char *to, RSA *rsa, int padding)
{
    return meth->rsa_priv_enc;
}

int RSA_meth_set_priv_enc(RSA_METHOD *meth,
                          int (*priv_enc) (int flen, const unsigned char *from,
                                           unsigned char *to, RSA *rsa,
                                           int padding))
{
    meth->rsa_priv_enc = priv_enc;
    return 1;
}

int (*RSA_meth_get_priv_dec(const RSA_METHOD *meth))
    (int flen, const unsigned char *from,
     unsigned char *to, RSA *rsa, int padding)
{
    return meth->rsa_priv_dec;
}

int RSA_meth_set_priv_dec(RSA_METHOD *meth,
                          int (*priv_dec) (int flen, const unsigned char *from,
                                           unsigned char *to, RSA *rsa,
                                           int padding))
{
    meth->rsa_priv_dec = priv_dec;
    return 1;
}

    /* Can be null */
int (*RSA_meth_get_mod_exp(const RSA_METHOD *meth))
    (BIGNUM *r0, const BIGNUM *i, RSA *rsa, BN_CTX *ctx)
{
    return meth->rsa_mod_exp;
}

int RSA_meth_set_mod_exp(RSA_METHOD *meth,
                         int (*mod_exp) (BIGNUM *r0, const BIGNUM *i, RSA *rsa,
                                         BN_CTX *ctx))
{
    meth->rsa_mod_exp = mod_exp;
    return 1;
}

    /* Can be null */
int (*RSA_meth_get_bn_mod_exp(const RSA_METHOD *meth))
    (BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
     const BIGNUM *m, BN_CTX *ctx, BN_MONT_CTX *m_ctx)
{
    return meth->bn_mod_exp;
}

int RSA_meth_set_bn_mod_exp(RSA_METHOD *meth,
                            int (*bn_mod_exp) (BIGNUM *r,
                                               const BIGNUM *a,
                                               const BIGNUM *p,
                                               const BIGNUM *m,
                                               BN_CTX *ctx,
                                               BN_MONT_CTX *m_ctx))
{
    meth->bn_mod_exp = bn_mod_exp;
    return 1;
}

    /* called at new */
int (*RSA_meth_get_init(const RSA_METHOD *meth)) (RSA *rsa)
{
    return meth->init;
}

int RSA_meth_set_init(RSA_METHOD *meth, int (*init) (RSA *rsa))
{
    meth->init = init;
    return 1;
}

    /* called at free */
int (*RSA_meth_get_finish(const RSA_METHOD *meth)) (RSA *rsa)
{
    return meth->finish;
}

int RSA_meth_set_finish(RSA_METHOD *meth, int (*finish) (RSA *rsa))
{
    meth->finish = finish;
    return 1;
}

int (*RSA_meth_get_sign(const RSA_METHOD *meth))
    (int type,
     const unsigned char *m, unsigned int m_length,
     unsigned char *sigret, unsigned int *siglen,
     const RSA *rsa)
{
    return meth->rsa_sign;
}

int RSA_meth_set_sign(RSA_METHOD *meth,
                      int (*sign) (int type, const unsigned char *m,
                                   unsigned int m_length,
                                   unsigned char *sigret, unsigned int *siglen,
                                   const RSA *rsa))
{
    meth->rsa_sign = sign;
    return 1;
}

int (*RSA_meth_get_verify(const RSA_METHOD *meth))
    (int dtype, const unsigned char *m,
     unsigned int m_length, const unsigned char *sigbuf,
     unsigned int siglen, const RSA *rsa)
{
    return meth->rsa_verify;
}

int RSA_meth_set_verify(RSA_METHOD *meth,
                        int (*verify) (int dtype, const unsigned char *m,
                                       unsigned int m_length,
                                       const unsigned char *sigbuf,
                                       unsigned int siglen, const RSA *rsa))
{
    meth->rsa_verify = verify;
    return 1;
}

int (*RSA_meth_get_keygen(const RSA_METHOD *meth))
    (RSA *rsa, int bits, BIGNUM *e, BN_GENCB *cb)
{
    return meth->rsa_keygen;
}

int RSA_meth_set_keygen(RSA_METHOD *meth,
                        int (*keygen) (RSA *rsa, int bits, BIGNUM *e,
                                       BN_GENCB *cb))
{
    meth->rsa_keygen = keygen;
    return 1;
}

int (*RSA_meth_get_multi_prime_keygen(const RSA_METHOD *meth))
    (RSA *rsa, int bits, int primes, BIGNUM *e, BN_GENCB *cb)
{
    return meth->rsa_multi_prime_keygen;
}

int RSA_meth_set_multi_prime_keygen(RSA_METHOD *meth,
                                    int (*keygen) (RSA *rsa, int bits,
                                                   int primes, BIGNUM *e,
                                                   BN_GENCB *cb))
{
    meth->rsa_multi_prime_keygen = keygen;
    return 1;
}
