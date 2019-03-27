/*
 * Copyright 2000-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/bn.h>
#include "dh_locl.h"
#include <openssl/objects.h>
#include <openssl/asn1t.h>

/* Override the default free and new methods */
static int dh_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it,
                 void *exarg)
{
    if (operation == ASN1_OP_NEW_PRE) {
        *pval = (ASN1_VALUE *)DH_new();
        if (*pval != NULL)
            return 2;
        return 0;
    } else if (operation == ASN1_OP_FREE_PRE) {
        DH_free((DH *)*pval);
        *pval = NULL;
        return 2;
    }
    return 1;
}

ASN1_SEQUENCE_cb(DHparams, dh_cb) = {
        ASN1_SIMPLE(DH, p, BIGNUM),
        ASN1_SIMPLE(DH, g, BIGNUM),
        ASN1_OPT_EMBED(DH, length, ZINT32),
} ASN1_SEQUENCE_END_cb(DH, DHparams)

IMPLEMENT_ASN1_ENCODE_FUNCTIONS_const_fname(DH, DHparams, DHparams)

/*
 * Internal only structures for handling X9.42 DH: this gets translated to or
 * from a DH structure straight away.
 */

typedef struct {
    ASN1_BIT_STRING *seed;
    BIGNUM *counter;
} int_dhvparams;

typedef struct {
    BIGNUM *p;
    BIGNUM *q;
    BIGNUM *g;
    BIGNUM *j;
    int_dhvparams *vparams;
} int_dhx942_dh;

ASN1_SEQUENCE(DHvparams) = {
        ASN1_SIMPLE(int_dhvparams, seed, ASN1_BIT_STRING),
        ASN1_SIMPLE(int_dhvparams, counter, BIGNUM)
} static_ASN1_SEQUENCE_END_name(int_dhvparams, DHvparams)

ASN1_SEQUENCE(DHxparams) = {
        ASN1_SIMPLE(int_dhx942_dh, p, BIGNUM),
        ASN1_SIMPLE(int_dhx942_dh, g, BIGNUM),
        ASN1_SIMPLE(int_dhx942_dh, q, BIGNUM),
        ASN1_OPT(int_dhx942_dh, j, BIGNUM),
        ASN1_OPT(int_dhx942_dh, vparams, DHvparams),
} static_ASN1_SEQUENCE_END_name(int_dhx942_dh, DHxparams)

int_dhx942_dh *d2i_int_dhx(int_dhx942_dh **a,
                           const unsigned char **pp, long length);
int i2d_int_dhx(const int_dhx942_dh *a, unsigned char **pp);

IMPLEMENT_ASN1_ENCODE_FUNCTIONS_const_fname(int_dhx942_dh, DHxparams, int_dhx)

/* Application public function: read in X9.42 DH parameters into DH structure */

DH *d2i_DHxparams(DH **a, const unsigned char **pp, long length)
{
    int_dhx942_dh *dhx = NULL;
    DH *dh = NULL;
    dh = DH_new();
    if (dh == NULL)
        return NULL;
    dhx = d2i_int_dhx(NULL, pp, length);
    if (dhx == NULL) {
        DH_free(dh);
        return NULL;
    }

    if (a) {
        DH_free(*a);
        *a = dh;
    }

    dh->p = dhx->p;
    dh->q = dhx->q;
    dh->g = dhx->g;
    dh->j = dhx->j;

    if (dhx->vparams) {
        dh->seed = dhx->vparams->seed->data;
        dh->seedlen = dhx->vparams->seed->length;
        dh->counter = dhx->vparams->counter;
        dhx->vparams->seed->data = NULL;
        ASN1_BIT_STRING_free(dhx->vparams->seed);
        OPENSSL_free(dhx->vparams);
        dhx->vparams = NULL;
    }

    OPENSSL_free(dhx);
    return dh;
}

int i2d_DHxparams(const DH *dh, unsigned char **pp)
{
    int_dhx942_dh dhx;
    int_dhvparams dhv;
    ASN1_BIT_STRING bs;
    dhx.p = dh->p;
    dhx.g = dh->g;
    dhx.q = dh->q;
    dhx.j = dh->j;
    if (dh->counter && dh->seed && dh->seedlen > 0) {
        bs.flags = ASN1_STRING_FLAG_BITS_LEFT;
        bs.data = dh->seed;
        bs.length = dh->seedlen;
        dhv.seed = &bs;
        dhv.counter = dh->counter;
        dhx.vparams = &dhv;
    } else
        dhx.vparams = NULL;

    return i2d_int_dhx(&dhx, pp);
}
