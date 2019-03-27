/*
 * Copyright 2006-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/evp.h>
#include <openssl/dsa.h>

#ifndef OPENSSL_NO_STDIO
int DSA_print_fp(FILE *fp, const DSA *x, int off)
{
    BIO *b;
    int ret;

    if ((b = BIO_new(BIO_s_file())) == NULL) {
        DSAerr(DSA_F_DSA_PRINT_FP, ERR_R_BUF_LIB);
        return 0;
    }
    BIO_set_fp(b, fp, BIO_NOCLOSE);
    ret = DSA_print(b, x, off);
    BIO_free(b);
    return ret;
}

int DSAparams_print_fp(FILE *fp, const DSA *x)
{
    BIO *b;
    int ret;

    if ((b = BIO_new(BIO_s_file())) == NULL) {
        DSAerr(DSA_F_DSAPARAMS_PRINT_FP, ERR_R_BUF_LIB);
        return 0;
    }
    BIO_set_fp(b, fp, BIO_NOCLOSE);
    ret = DSAparams_print(b, x);
    BIO_free(b);
    return ret;
}
#endif

int DSA_print(BIO *bp, const DSA *x, int off)
{
    EVP_PKEY *pk;
    int ret;
    pk = EVP_PKEY_new();
    if (pk == NULL || !EVP_PKEY_set1_DSA(pk, (DSA *)x))
        return 0;
    ret = EVP_PKEY_print_private(bp, pk, off, NULL);
    EVP_PKEY_free(pk);
    return ret;
}

int DSAparams_print(BIO *bp, const DSA *x)
{
    EVP_PKEY *pk;
    int ret;
    pk = EVP_PKEY_new();
    if (pk == NULL || !EVP_PKEY_set1_DSA(pk, (DSA *)x))
        return 0;
    ret = EVP_PKEY_print_params(bp, pk, 4, NULL);
    EVP_PKEY_free(pk);
    return ret;
}
