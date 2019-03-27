/*
 * Copyright 2015-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <string.h>
#include <openssl/ec.h>
#include <openssl/engine.h>
#include <openssl/err.h>
#include "ec_lcl.h"


static const EC_KEY_METHOD openssl_ec_key_method = {
    "OpenSSL EC_KEY method",
    0,
    0,0,0,0,0,0,
    ossl_ec_key_gen,
    ossl_ecdh_compute_key,
    ossl_ecdsa_sign,
    ossl_ecdsa_sign_setup,
    ossl_ecdsa_sign_sig,
    ossl_ecdsa_verify,
    ossl_ecdsa_verify_sig
};

static const EC_KEY_METHOD *default_ec_key_meth = &openssl_ec_key_method;

const EC_KEY_METHOD *EC_KEY_OpenSSL(void)
{
    return &openssl_ec_key_method;
}

const EC_KEY_METHOD *EC_KEY_get_default_method(void)
{
    return default_ec_key_meth;
}

void EC_KEY_set_default_method(const EC_KEY_METHOD *meth)
{
    if (meth == NULL)
        default_ec_key_meth = &openssl_ec_key_method;
    else
        default_ec_key_meth = meth;
}

const EC_KEY_METHOD *EC_KEY_get_method(const EC_KEY *key)
{
    return key->meth;
}

int EC_KEY_set_method(EC_KEY *key, const EC_KEY_METHOD *meth)
{
    void (*finish)(EC_KEY *key) = key->meth->finish;

    if (finish != NULL)
        finish(key);

#ifndef OPENSSL_NO_ENGINE
    ENGINE_finish(key->engine);
    key->engine = NULL;
#endif

    key->meth = meth;
    if (meth->init != NULL)
        return meth->init(key);
    return 1;
}

EC_KEY *EC_KEY_new_method(ENGINE *engine)
{
    EC_KEY *ret = OPENSSL_zalloc(sizeof(*ret));

    if (ret == NULL) {
        ECerr(EC_F_EC_KEY_NEW_METHOD, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    ret->references = 1;
    ret->lock = CRYPTO_THREAD_lock_new();
    if (ret->lock == NULL) {
        ECerr(EC_F_EC_KEY_NEW_METHOD, ERR_R_MALLOC_FAILURE);
        OPENSSL_free(ret);
        return NULL;
    }

    ret->meth = EC_KEY_get_default_method();
#ifndef OPENSSL_NO_ENGINE
    if (engine != NULL) {
        if (!ENGINE_init(engine)) {
            ECerr(EC_F_EC_KEY_NEW_METHOD, ERR_R_ENGINE_LIB);
            goto err;
        }
        ret->engine = engine;
    } else
        ret->engine = ENGINE_get_default_EC();
    if (ret->engine != NULL) {
        ret->meth = ENGINE_get_EC(ret->engine);
        if (ret->meth == NULL) {
            ECerr(EC_F_EC_KEY_NEW_METHOD, ERR_R_ENGINE_LIB);
            goto err;
        }
    }
#endif

    ret->version = 1;
    ret->conv_form = POINT_CONVERSION_UNCOMPRESSED;

    if (!CRYPTO_new_ex_data(CRYPTO_EX_INDEX_EC_KEY, ret, &ret->ex_data)) {
        goto err;
    }

    if (ret->meth->init != NULL && ret->meth->init(ret) == 0) {
        ECerr(EC_F_EC_KEY_NEW_METHOD, ERR_R_INIT_FAIL);
        goto err;
    }
    return ret;

 err:
    EC_KEY_free(ret);
    return NULL;
}

int ECDH_compute_key(void *out, size_t outlen, const EC_POINT *pub_key,
                     const EC_KEY *eckey,
                     void *(*KDF) (const void *in, size_t inlen, void *out,
                                   size_t *outlen))
{
    unsigned char *sec = NULL;
    size_t seclen;
    if (eckey->meth->compute_key == NULL) {
        ECerr(EC_F_ECDH_COMPUTE_KEY, EC_R_OPERATION_NOT_SUPPORTED);
        return 0;
    }
    if (outlen > INT_MAX) {
        ECerr(EC_F_ECDH_COMPUTE_KEY, EC_R_INVALID_OUTPUT_LENGTH);
        return 0;
    }
    if (!eckey->meth->compute_key(&sec, &seclen, pub_key, eckey))
        return 0;
    if (KDF != NULL) {
        KDF(sec, seclen, out, &outlen);
    } else {
        if (outlen > seclen)
            outlen = seclen;
        memcpy(out, sec, outlen);
    }
    OPENSSL_clear_free(sec, seclen);
    return outlen;
}

EC_KEY_METHOD *EC_KEY_METHOD_new(const EC_KEY_METHOD *meth)
{
    EC_KEY_METHOD *ret = OPENSSL_zalloc(sizeof(*meth));

    if (ret == NULL)
        return NULL;
    if (meth != NULL)
        *ret = *meth;
    ret->flags |= EC_KEY_METHOD_DYNAMIC;
    return ret;
}

void EC_KEY_METHOD_free(EC_KEY_METHOD *meth)
{
    if (meth->flags & EC_KEY_METHOD_DYNAMIC)
        OPENSSL_free(meth);
}

void EC_KEY_METHOD_set_init(EC_KEY_METHOD *meth,
                            int (*init)(EC_KEY *key),
                            void (*finish)(EC_KEY *key),
                            int (*copy)(EC_KEY *dest, const EC_KEY *src),
                            int (*set_group)(EC_KEY *key, const EC_GROUP *grp),
                            int (*set_private)(EC_KEY *key,
                                               const BIGNUM *priv_key),
                            int (*set_public)(EC_KEY *key,
                                              const EC_POINT *pub_key))
{
    meth->init = init;
    meth->finish = finish;
    meth->copy = copy;
    meth->set_group = set_group;
    meth->set_private = set_private;
    meth->set_public = set_public;
}

void EC_KEY_METHOD_set_keygen(EC_KEY_METHOD *meth,
                              int (*keygen)(EC_KEY *key))
{
    meth->keygen = keygen;
}

void EC_KEY_METHOD_set_compute_key(EC_KEY_METHOD *meth,
                                   int (*ckey)(unsigned char **psec,
                                               size_t *pseclen,
                                               const EC_POINT *pub_key,
                                               const EC_KEY *ecdh))
{
    meth->compute_key = ckey;
}

void EC_KEY_METHOD_set_sign(EC_KEY_METHOD *meth,
                            int (*sign)(int type, const unsigned char *dgst,
                                        int dlen, unsigned char *sig,
                                        unsigned int *siglen,
                                        const BIGNUM *kinv, const BIGNUM *r,
                                        EC_KEY *eckey),
                            int (*sign_setup)(EC_KEY *eckey, BN_CTX *ctx_in,
                                              BIGNUM **kinvp, BIGNUM **rp),
                            ECDSA_SIG *(*sign_sig)(const unsigned char *dgst,
                                                   int dgst_len,
                                                   const BIGNUM *in_kinv,
                                                   const BIGNUM *in_r,
                                                   EC_KEY *eckey))
{
    meth->sign = sign;
    meth->sign_setup = sign_setup;
    meth->sign_sig = sign_sig;
}

void EC_KEY_METHOD_set_verify(EC_KEY_METHOD *meth,
                              int (*verify)(int type, const unsigned
                                            char *dgst, int dgst_len,
                                            const unsigned char *sigbuf,
                                            int sig_len, EC_KEY *eckey),
                              int (*verify_sig)(const unsigned char *dgst,
                                                int dgst_len,
                                                const ECDSA_SIG *sig,
                                                EC_KEY *eckey))
{
    meth->verify = verify;
    meth->verify_sig = verify_sig;
}

void EC_KEY_METHOD_get_init(const EC_KEY_METHOD *meth,
                            int (**pinit)(EC_KEY *key),
                            void (**pfinish)(EC_KEY *key),
                            int (**pcopy)(EC_KEY *dest, const EC_KEY *src),
                            int (**pset_group)(EC_KEY *key,
                                               const EC_GROUP *grp),
                            int (**pset_private)(EC_KEY *key,
                                                 const BIGNUM *priv_key),
                            int (**pset_public)(EC_KEY *key,
                                                const EC_POINT *pub_key))
{
    if (pinit != NULL)
        *pinit = meth->init;
    if (pfinish != NULL)
        *pfinish = meth->finish;
    if (pcopy != NULL)
        *pcopy = meth->copy;
    if (pset_group != NULL)
        *pset_group = meth->set_group;
    if (pset_private != NULL)
        *pset_private = meth->set_private;
    if (pset_public != NULL)
        *pset_public = meth->set_public;
}

void EC_KEY_METHOD_get_keygen(const EC_KEY_METHOD *meth,
                              int (**pkeygen)(EC_KEY *key))
{
    if (pkeygen != NULL)
        *pkeygen = meth->keygen;
}

void EC_KEY_METHOD_get_compute_key(const EC_KEY_METHOD *meth,
                                   int (**pck)(unsigned char **pout,
                                               size_t *poutlen,
                                               const EC_POINT *pub_key,
                                               const EC_KEY *ecdh))
{
    if (pck != NULL)
        *pck = meth->compute_key;
}

void EC_KEY_METHOD_get_sign(const EC_KEY_METHOD *meth,
                            int (**psign)(int type, const unsigned char *dgst,
                                          int dlen, unsigned char *sig,
                                          unsigned int *siglen,
                                          const BIGNUM *kinv, const BIGNUM *r,
                                          EC_KEY *eckey),
                            int (**psign_setup)(EC_KEY *eckey, BN_CTX *ctx_in,
                                                BIGNUM **kinvp, BIGNUM **rp),
                            ECDSA_SIG *(**psign_sig)(const unsigned char *dgst,
                                                     int dgst_len,
                                                     const BIGNUM *in_kinv,
                                                     const BIGNUM *in_r,
                                                     EC_KEY *eckey))
{
    if (psign != NULL)
        *psign = meth->sign;
    if (psign_setup != NULL)
        *psign_setup = meth->sign_setup;
    if (psign_sig != NULL)
        *psign_sig = meth->sign_sig;
}

void EC_KEY_METHOD_get_verify(const EC_KEY_METHOD *meth,
                              int (**pverify)(int type, const unsigned
                                              char *dgst, int dgst_len,
                                              const unsigned char *sigbuf,
                                              int sig_len, EC_KEY *eckey),
                              int (**pverify_sig)(const unsigned char *dgst,
                                                  int dgst_len,
                                                  const ECDSA_SIG *sig,
                                                  EC_KEY *eckey))
{
    if (pverify != NULL)
        *pverify = meth->verify;
    if (pverify_sig != NULL)
        *pverify_sig = meth->verify_sig;
}
