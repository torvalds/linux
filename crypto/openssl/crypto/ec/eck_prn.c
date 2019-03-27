/*
 * Copyright 2006-2018 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright (c) 2002, Oracle and/or its affiliates. All rights reserved
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/bn.h>

#ifndef OPENSSL_NO_STDIO
int ECPKParameters_print_fp(FILE *fp, const EC_GROUP *x, int off)
{
    BIO *b;
    int ret;

    if ((b = BIO_new(BIO_s_file())) == NULL) {
        ECerr(EC_F_ECPKPARAMETERS_PRINT_FP, ERR_R_BUF_LIB);
        return 0;
    }
    BIO_set_fp(b, fp, BIO_NOCLOSE);
    ret = ECPKParameters_print(b, x, off);
    BIO_free(b);
    return ret;
}

int EC_KEY_print_fp(FILE *fp, const EC_KEY *x, int off)
{
    BIO *b;
    int ret;

    if ((b = BIO_new(BIO_s_file())) == NULL) {
        ECerr(EC_F_EC_KEY_PRINT_FP, ERR_R_BIO_LIB);
        return 0;
    }
    BIO_set_fp(b, fp, BIO_NOCLOSE);
    ret = EC_KEY_print(b, x, off);
    BIO_free(b);
    return ret;
}

int ECParameters_print_fp(FILE *fp, const EC_KEY *x)
{
    BIO *b;
    int ret;

    if ((b = BIO_new(BIO_s_file())) == NULL) {
        ECerr(EC_F_ECPARAMETERS_PRINT_FP, ERR_R_BIO_LIB);
        return 0;
    }
    BIO_set_fp(b, fp, BIO_NOCLOSE);
    ret = ECParameters_print(b, x);
    BIO_free(b);
    return ret;
}
#endif

static int print_bin(BIO *fp, const char *str, const unsigned char *num,
                     size_t len, int off);

int ECPKParameters_print(BIO *bp, const EC_GROUP *x, int off)
{
    int ret = 0, reason = ERR_R_BIO_LIB;
    BN_CTX *ctx = NULL;
    const EC_POINT *point = NULL;
    BIGNUM *p = NULL, *a = NULL, *b = NULL, *gen = NULL;
    const BIGNUM *order = NULL, *cofactor = NULL;
    const unsigned char *seed;
    size_t seed_len = 0;

    static const char *gen_compressed = "Generator (compressed):";
    static const char *gen_uncompressed = "Generator (uncompressed):";
    static const char *gen_hybrid = "Generator (hybrid):";

    if (!x) {
        reason = ERR_R_PASSED_NULL_PARAMETER;
        goto err;
    }

    ctx = BN_CTX_new();
    if (ctx == NULL) {
        reason = ERR_R_MALLOC_FAILURE;
        goto err;
    }

    if (EC_GROUP_get_asn1_flag(x)) {
        /* the curve parameter are given by an asn1 OID */
        int nid;
        const char *nname;

        if (!BIO_indent(bp, off, 128))
            goto err;

        nid = EC_GROUP_get_curve_name(x);
        if (nid == 0)
            goto err;
        if (BIO_printf(bp, "ASN1 OID: %s", OBJ_nid2sn(nid)) <= 0)
            goto err;
        if (BIO_printf(bp, "\n") <= 0)
            goto err;
        nname = EC_curve_nid2nist(nid);
        if (nname) {
            if (!BIO_indent(bp, off, 128))
                goto err;
            if (BIO_printf(bp, "NIST CURVE: %s\n", nname) <= 0)
                goto err;
        }
    } else {
        /* explicit parameters */
        int is_char_two = 0;
        point_conversion_form_t form;
        int tmp_nid = EC_METHOD_get_field_type(EC_GROUP_method_of(x));

        if (tmp_nid == NID_X9_62_characteristic_two_field)
            is_char_two = 1;

        if ((p = BN_new()) == NULL || (a = BN_new()) == NULL ||
            (b = BN_new()) == NULL) {
            reason = ERR_R_MALLOC_FAILURE;
            goto err;
        }

        if (!EC_GROUP_get_curve(x, p, a, b, ctx)) {
            reason = ERR_R_EC_LIB;
            goto err;
        }

        if ((point = EC_GROUP_get0_generator(x)) == NULL) {
            reason = ERR_R_EC_LIB;
            goto err;
        }
        order = EC_GROUP_get0_order(x);
        cofactor = EC_GROUP_get0_cofactor(x);
        if (order == NULL) {
            reason = ERR_R_EC_LIB;
            goto err;
        }

        form = EC_GROUP_get_point_conversion_form(x);

        if ((gen = EC_POINT_point2bn(x, point, form, NULL, ctx)) == NULL) {
            reason = ERR_R_EC_LIB;
            goto err;
        }

        if ((seed = EC_GROUP_get0_seed(x)) != NULL)
            seed_len = EC_GROUP_get_seed_len(x);

        if (!BIO_indent(bp, off, 128))
            goto err;

        /* print the 'short name' of the field type */
        if (BIO_printf(bp, "Field Type: %s\n", OBJ_nid2sn(tmp_nid))
            <= 0)
            goto err;

        if (is_char_two) {
            /* print the 'short name' of the base type OID */
            int basis_type = EC_GROUP_get_basis_type(x);
            if (basis_type == 0)
                goto err;

            if (!BIO_indent(bp, off, 128))
                goto err;

            if (BIO_printf(bp, "Basis Type: %s\n",
                           OBJ_nid2sn(basis_type)) <= 0)
                goto err;

            /* print the polynomial */
            if ((p != NULL) && !ASN1_bn_print(bp, "Polynomial:", p, NULL,
                                              off))
                goto err;
        } else {
            if ((p != NULL) && !ASN1_bn_print(bp, "Prime:", p, NULL, off))
                goto err;
        }
        if ((a != NULL) && !ASN1_bn_print(bp, "A:   ", a, NULL, off))
            goto err;
        if ((b != NULL) && !ASN1_bn_print(bp, "B:   ", b, NULL, off))
            goto err;
        if (form == POINT_CONVERSION_COMPRESSED) {
            if ((gen != NULL) && !ASN1_bn_print(bp, gen_compressed, gen,
                                                NULL, off))
                goto err;
        } else if (form == POINT_CONVERSION_UNCOMPRESSED) {
            if ((gen != NULL) && !ASN1_bn_print(bp, gen_uncompressed, gen,
                                                NULL, off))
                goto err;
        } else {                /* form == POINT_CONVERSION_HYBRID */

            if ((gen != NULL) && !ASN1_bn_print(bp, gen_hybrid, gen,
                                                NULL, off))
                goto err;
        }
        if ((order != NULL) && !ASN1_bn_print(bp, "Order: ", order,
                                              NULL, off))
            goto err;
        if ((cofactor != NULL) && !ASN1_bn_print(bp, "Cofactor: ", cofactor,
                                                 NULL, off))
            goto err;
        if (seed && !print_bin(bp, "Seed:", seed, seed_len, off))
            goto err;
    }
    ret = 1;
 err:
    if (!ret)
        ECerr(EC_F_ECPKPARAMETERS_PRINT, reason);
    BN_free(p);
    BN_free(a);
    BN_free(b);
    BN_free(gen);
    BN_CTX_free(ctx);
    return ret;
}

static int print_bin(BIO *fp, const char *name, const unsigned char *buf,
                     size_t len, int off)
{
    size_t i;
    char str[128 + 1 + 4];

    if (buf == NULL)
        return 1;
    if (off > 0) {
        if (off > 128)
            off = 128;
        memset(str, ' ', off);
        if (BIO_write(fp, str, off) <= 0)
            return 0;
    } else {
        off = 0;
    }

    if (BIO_printf(fp, "%s", name) <= 0)
        return 0;

    for (i = 0; i < len; i++) {
        if ((i % 15) == 0) {
            str[0] = '\n';
            memset(&(str[1]), ' ', off + 4);
            if (BIO_write(fp, str, off + 1 + 4) <= 0)
                return 0;
        }
        if (BIO_printf(fp, "%02x%s", buf[i], ((i + 1) == len) ? "" : ":") <=
            0)
            return 0;
    }
    if (BIO_write(fp, "\n", 1) <= 0)
        return 0;

    return 1;
}
