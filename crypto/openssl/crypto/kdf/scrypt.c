/*
 * Copyright 2017-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdlib.h>
#include <string.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/evp.h>
#include "internal/cryptlib.h"
#include "internal/evp_int.h"

#ifndef OPENSSL_NO_SCRYPT

static int atou64(const char *nptr, uint64_t *result);

typedef struct {
    unsigned char *pass;
    size_t pass_len;
    unsigned char *salt;
    size_t salt_len;
    uint64_t N, r, p;
    uint64_t maxmem_bytes;
} SCRYPT_PKEY_CTX;

/* Custom uint64_t parser since we do not have strtoull */
static int atou64(const char *nptr, uint64_t *result)
{
    uint64_t value = 0;

    while (*nptr) {
        unsigned int digit;
        uint64_t new_value;

        if ((*nptr < '0') || (*nptr > '9')) {
            return 0;
        }
        digit = (unsigned int)(*nptr - '0');
        new_value = (value * 10) + digit;
        if ((new_value < digit) || ((new_value - digit) / 10 != value)) {
            /* Overflow */
            return 0;
        }
        value = new_value;
        nptr++;
    }
    *result = value;
    return 1;
}

static int pkey_scrypt_init(EVP_PKEY_CTX *ctx)
{
    SCRYPT_PKEY_CTX *kctx;

    kctx = OPENSSL_zalloc(sizeof(*kctx));
    if (kctx == NULL) {
        KDFerr(KDF_F_PKEY_SCRYPT_INIT, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    /* Default values are the most conservative recommendation given in the
     * original paper of C. Percival. Derivation uses roughly 1 GiB of memory
     * for this parameter choice (approx. 128 * r * (N + p) bytes).
     */
    kctx->N = 1 << 20;
    kctx->r = 8;
    kctx->p = 1;
    kctx->maxmem_bytes = 1025 * 1024 * 1024;

    ctx->data = kctx;

    return 1;
}

static void pkey_scrypt_cleanup(EVP_PKEY_CTX *ctx)
{
    SCRYPT_PKEY_CTX *kctx = ctx->data;

    OPENSSL_clear_free(kctx->salt, kctx->salt_len);
    OPENSSL_clear_free(kctx->pass, kctx->pass_len);
    OPENSSL_free(kctx);
}

static int pkey_scrypt_set_membuf(unsigned char **buffer, size_t *buflen,
                                  const unsigned char *new_buffer,
                                  const int new_buflen)
{
    if (new_buffer == NULL)
        return 1;

    if (new_buflen < 0)
        return 0;

    if (*buffer != NULL)
        OPENSSL_clear_free(*buffer, *buflen);

    if (new_buflen > 0) {
        *buffer = OPENSSL_memdup(new_buffer, new_buflen);
    } else {
        *buffer = OPENSSL_malloc(1);
    }
    if (*buffer == NULL) {
        KDFerr(KDF_F_PKEY_SCRYPT_SET_MEMBUF, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    *buflen = new_buflen;
    return 1;
}

static int is_power_of_two(uint64_t value)
{
    return (value != 0) && ((value & (value - 1)) == 0);
}

static int pkey_scrypt_ctrl(EVP_PKEY_CTX *ctx, int type, int p1, void *p2)
{
    SCRYPT_PKEY_CTX *kctx = ctx->data;
    uint64_t u64_value;

    switch (type) {
    case EVP_PKEY_CTRL_PASS:
        return pkey_scrypt_set_membuf(&kctx->pass, &kctx->pass_len, p2, p1);

    case EVP_PKEY_CTRL_SCRYPT_SALT:
        return pkey_scrypt_set_membuf(&kctx->salt, &kctx->salt_len, p2, p1);

    case EVP_PKEY_CTRL_SCRYPT_N:
        u64_value = *((uint64_t *)p2);
        if ((u64_value <= 1) || !is_power_of_two(u64_value))
            return 0;
        kctx->N = u64_value;
        return 1;

    case EVP_PKEY_CTRL_SCRYPT_R:
        u64_value = *((uint64_t *)p2);
        if (u64_value < 1)
            return 0;
        kctx->r = u64_value;
        return 1;

    case EVP_PKEY_CTRL_SCRYPT_P:
        u64_value = *((uint64_t *)p2);
        if (u64_value < 1)
            return 0;
        kctx->p = u64_value;
        return 1;

    case EVP_PKEY_CTRL_SCRYPT_MAXMEM_BYTES:
        u64_value = *((uint64_t *)p2);
        if (u64_value < 1)
            return 0;
        kctx->maxmem_bytes = u64_value;
        return 1;

    default:
        return -2;

    }
}

static int pkey_scrypt_ctrl_uint64(EVP_PKEY_CTX *ctx, int type,
                                   const char *value)
{
    uint64_t int_value;

    if (!atou64(value, &int_value)) {
        KDFerr(KDF_F_PKEY_SCRYPT_CTRL_UINT64, KDF_R_VALUE_ERROR);
        return 0;
    }
    return pkey_scrypt_ctrl(ctx, type, 0, &int_value);
}

static int pkey_scrypt_ctrl_str(EVP_PKEY_CTX *ctx, const char *type,
                                const char *value)
{
    if (value == NULL) {
        KDFerr(KDF_F_PKEY_SCRYPT_CTRL_STR, KDF_R_VALUE_MISSING);
        return 0;
    }

    if (strcmp(type, "pass") == 0)
        return EVP_PKEY_CTX_str2ctrl(ctx, EVP_PKEY_CTRL_PASS, value);

    if (strcmp(type, "hexpass") == 0)
        return EVP_PKEY_CTX_hex2ctrl(ctx, EVP_PKEY_CTRL_PASS, value);

    if (strcmp(type, "salt") == 0)
        return EVP_PKEY_CTX_str2ctrl(ctx, EVP_PKEY_CTRL_SCRYPT_SALT, value);

    if (strcmp(type, "hexsalt") == 0)
        return EVP_PKEY_CTX_hex2ctrl(ctx, EVP_PKEY_CTRL_SCRYPT_SALT, value);

    if (strcmp(type, "N") == 0)
        return pkey_scrypt_ctrl_uint64(ctx, EVP_PKEY_CTRL_SCRYPT_N, value);

    if (strcmp(type, "r") == 0)
        return pkey_scrypt_ctrl_uint64(ctx, EVP_PKEY_CTRL_SCRYPT_R, value);

    if (strcmp(type, "p") == 0)
        return pkey_scrypt_ctrl_uint64(ctx, EVP_PKEY_CTRL_SCRYPT_P, value);

    if (strcmp(type, "maxmem_bytes") == 0)
        return pkey_scrypt_ctrl_uint64(ctx, EVP_PKEY_CTRL_SCRYPT_MAXMEM_BYTES,
                                       value);

    KDFerr(KDF_F_PKEY_SCRYPT_CTRL_STR, KDF_R_UNKNOWN_PARAMETER_TYPE);
    return -2;
}

static int pkey_scrypt_derive(EVP_PKEY_CTX *ctx, unsigned char *key,
                              size_t *keylen)
{
    SCRYPT_PKEY_CTX *kctx = ctx->data;

    if (kctx->pass == NULL) {
        KDFerr(KDF_F_PKEY_SCRYPT_DERIVE, KDF_R_MISSING_PASS);
        return 0;
    }

    if (kctx->salt == NULL) {
        KDFerr(KDF_F_PKEY_SCRYPT_DERIVE, KDF_R_MISSING_SALT);
        return 0;
    }

    return EVP_PBE_scrypt((char *)kctx->pass, kctx->pass_len, kctx->salt,
                          kctx->salt_len, kctx->N, kctx->r, kctx->p,
                          kctx->maxmem_bytes, key, *keylen);
}

const EVP_PKEY_METHOD scrypt_pkey_meth = {
    EVP_PKEY_SCRYPT,
    0,
    pkey_scrypt_init,
    0,
    pkey_scrypt_cleanup,

    0, 0,
    0, 0,

    0,
    0,

    0,
    0,

    0, 0,

    0, 0, 0, 0,

    0, 0,

    0, 0,

    0,
    pkey_scrypt_derive,
    pkey_scrypt_ctrl,
    pkey_scrypt_ctrl_str
};

#endif
