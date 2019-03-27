/*
 * Copyright 2000-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/bn.h>
#include <openssl/x509.h>
#include <openssl/asn1t.h>
#include "rsa_locl.h"

/*
 * Override the default free and new methods,
 * and calculate helper products for multi-prime
 * RSA keys.
 */
static int rsa_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it,
                  void *exarg)
{
    if (operation == ASN1_OP_NEW_PRE) {
        *pval = (ASN1_VALUE *)RSA_new();
        if (*pval != NULL)
            return 2;
        return 0;
    } else if (operation == ASN1_OP_FREE_PRE) {
        RSA_free((RSA *)*pval);
        *pval = NULL;
        return 2;
    } else if (operation == ASN1_OP_D2I_POST) {
        if (((RSA *)*pval)->version != RSA_ASN1_VERSION_MULTI) {
            /* not a multi-prime key, skip */
            return 1;
        }
        return (rsa_multip_calc_product((RSA *)*pval) == 1) ? 2 : 0;
    }
    return 1;
}

/* Based on definitions in RFC 8017 appendix A.1.2 */
ASN1_SEQUENCE(RSA_PRIME_INFO) = {
        ASN1_SIMPLE(RSA_PRIME_INFO, r, CBIGNUM),
        ASN1_SIMPLE(RSA_PRIME_INFO, d, CBIGNUM),
        ASN1_SIMPLE(RSA_PRIME_INFO, t, CBIGNUM),
} ASN1_SEQUENCE_END(RSA_PRIME_INFO)

ASN1_SEQUENCE_cb(RSAPrivateKey, rsa_cb) = {
        ASN1_EMBED(RSA, version, INT32),
        ASN1_SIMPLE(RSA, n, BIGNUM),
        ASN1_SIMPLE(RSA, e, BIGNUM),
        ASN1_SIMPLE(RSA, d, CBIGNUM),
        ASN1_SIMPLE(RSA, p, CBIGNUM),
        ASN1_SIMPLE(RSA, q, CBIGNUM),
        ASN1_SIMPLE(RSA, dmp1, CBIGNUM),
        ASN1_SIMPLE(RSA, dmq1, CBIGNUM),
        ASN1_SIMPLE(RSA, iqmp, CBIGNUM),
        ASN1_SEQUENCE_OF_OPT(RSA, prime_infos, RSA_PRIME_INFO)
} ASN1_SEQUENCE_END_cb(RSA, RSAPrivateKey)


ASN1_SEQUENCE_cb(RSAPublicKey, rsa_cb) = {
        ASN1_SIMPLE(RSA, n, BIGNUM),
        ASN1_SIMPLE(RSA, e, BIGNUM),
} ASN1_SEQUENCE_END_cb(RSA, RSAPublicKey)

/* Free up maskHash */
static int rsa_pss_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it,
                      void *exarg)
{
    if (operation == ASN1_OP_FREE_PRE) {
        RSA_PSS_PARAMS *pss = (RSA_PSS_PARAMS *)*pval;
        X509_ALGOR_free(pss->maskHash);
    }
    return 1;
}

ASN1_SEQUENCE_cb(RSA_PSS_PARAMS, rsa_pss_cb) = {
        ASN1_EXP_OPT(RSA_PSS_PARAMS, hashAlgorithm, X509_ALGOR,0),
        ASN1_EXP_OPT(RSA_PSS_PARAMS, maskGenAlgorithm, X509_ALGOR,1),
        ASN1_EXP_OPT(RSA_PSS_PARAMS, saltLength, ASN1_INTEGER,2),
        ASN1_EXP_OPT(RSA_PSS_PARAMS, trailerField, ASN1_INTEGER,3)
} ASN1_SEQUENCE_END_cb(RSA_PSS_PARAMS, RSA_PSS_PARAMS)

IMPLEMENT_ASN1_FUNCTIONS(RSA_PSS_PARAMS)

/* Free up maskHash */
static int rsa_oaep_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it,
                       void *exarg)
{
    if (operation == ASN1_OP_FREE_PRE) {
        RSA_OAEP_PARAMS *oaep = (RSA_OAEP_PARAMS *)*pval;
        X509_ALGOR_free(oaep->maskHash);
    }
    return 1;
}

ASN1_SEQUENCE_cb(RSA_OAEP_PARAMS, rsa_oaep_cb) = {
        ASN1_EXP_OPT(RSA_OAEP_PARAMS, hashFunc, X509_ALGOR, 0),
        ASN1_EXP_OPT(RSA_OAEP_PARAMS, maskGenFunc, X509_ALGOR, 1),
        ASN1_EXP_OPT(RSA_OAEP_PARAMS, pSourceFunc, X509_ALGOR, 2),
} ASN1_SEQUENCE_END_cb(RSA_OAEP_PARAMS, RSA_OAEP_PARAMS)

IMPLEMENT_ASN1_FUNCTIONS(RSA_OAEP_PARAMS)

IMPLEMENT_ASN1_ENCODE_FUNCTIONS_const_fname(RSA, RSAPrivateKey, RSAPrivateKey)

IMPLEMENT_ASN1_ENCODE_FUNCTIONS_const_fname(RSA, RSAPublicKey, RSAPublicKey)

RSA *RSAPublicKey_dup(RSA *rsa)
{
    return ASN1_item_dup(ASN1_ITEM_rptr(RSAPublicKey), rsa);
}

RSA *RSAPrivateKey_dup(RSA *rsa)
{
    return ASN1_item_dup(ASN1_ITEM_rptr(RSAPrivateKey), rsa);
}
