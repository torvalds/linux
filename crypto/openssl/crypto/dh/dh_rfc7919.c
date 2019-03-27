/*
 * Copyright 2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include "dh_locl.h"
#include <openssl/bn.h>
#include <openssl/objects.h>
#include "internal/bn_dh.h"

static DH *dh_param_init(const BIGNUM *p, int32_t nbits)
{
    DH *dh = DH_new();
    if (dh == NULL)
        return NULL;
    dh->p = (BIGNUM *)p;
    dh->g = (BIGNUM *)&_bignum_const_2;
    dh->length = nbits;
    return dh;
}

DH *DH_new_by_nid(int nid)
{
    switch (nid) {
    case NID_ffdhe2048:
        return dh_param_init(&_bignum_ffdhe2048_p, 225);
    case NID_ffdhe3072:
        return dh_param_init(&_bignum_ffdhe3072_p, 275);
    case NID_ffdhe4096:
        return dh_param_init(&_bignum_ffdhe4096_p, 325);
    case NID_ffdhe6144:
        return dh_param_init(&_bignum_ffdhe6144_p, 375);
    case NID_ffdhe8192:
        return dh_param_init(&_bignum_ffdhe8192_p, 400);
    default:
        DHerr(DH_F_DH_NEW_BY_NID, DH_R_INVALID_PARAMETER_NID);
        return NULL;
    }
}

int DH_get_nid(const DH *dh)
{
    int nid;

    if (BN_get_word(dh->g) != 2)
        return NID_undef;
    if (!BN_cmp(dh->p, &_bignum_ffdhe2048_p))
        nid = NID_ffdhe2048;
    else if (!BN_cmp(dh->p, &_bignum_ffdhe3072_p))
        nid = NID_ffdhe3072;
    else if (!BN_cmp(dh->p, &_bignum_ffdhe4096_p))
        nid = NID_ffdhe4096;
    else if (!BN_cmp(dh->p, &_bignum_ffdhe6144_p))
        nid = NID_ffdhe6144;
    else if (!BN_cmp(dh->p, &_bignum_ffdhe8192_p))
        nid = NID_ffdhe8192;
    else
        return NID_undef;
    if (dh->q != NULL) {
        BIGNUM *q = BN_dup(dh->p);

        /* Check q = p * 2 + 1 we already know q is odd, so just shift right */
        if (q == NULL || !BN_rshift1(q, q) || !BN_cmp(dh->q, q))
            nid = NID_undef;
        BN_free(q);
    }
    return nid;
}
