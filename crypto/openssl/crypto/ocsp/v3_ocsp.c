/*
 * Copyright 2000-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

# include <stdio.h>
# include "internal/cryptlib.h"
# include <openssl/conf.h>
# include <openssl/asn1.h>
# include <openssl/ocsp.h>
# include "ocsp_lcl.h"
# include <openssl/x509v3.h>
# include "../x509v3/ext_dat.h"

/*
 * OCSP extensions and a couple of CRL entry extensions
 */

static int i2r_ocsp_crlid(const X509V3_EXT_METHOD *method, void *nonce,
                          BIO *out, int indent);
static int i2r_ocsp_acutoff(const X509V3_EXT_METHOD *method, void *nonce,
                            BIO *out, int indent);
static int i2r_object(const X509V3_EXT_METHOD *method, void *obj, BIO *out,
                      int indent);

static void *ocsp_nonce_new(void);
static int i2d_ocsp_nonce(void *a, unsigned char **pp);
static void *d2i_ocsp_nonce(void *a, const unsigned char **pp, long length);
static void ocsp_nonce_free(void *a);
static int i2r_ocsp_nonce(const X509V3_EXT_METHOD *method, void *nonce,
                          BIO *out, int indent);

static int i2r_ocsp_nocheck(const X509V3_EXT_METHOD *method,
                            void *nocheck, BIO *out, int indent);
static void *s2i_ocsp_nocheck(const X509V3_EXT_METHOD *method,
                              X509V3_CTX *ctx, const char *str);
static int i2r_ocsp_serviceloc(const X509V3_EXT_METHOD *method, void *in,
                               BIO *bp, int ind);

const X509V3_EXT_METHOD v3_ocsp_crlid = {
    NID_id_pkix_OCSP_CrlID, 0, ASN1_ITEM_ref(OCSP_CRLID),
    0, 0, 0, 0,
    0, 0,
    0, 0,
    i2r_ocsp_crlid, 0,
    NULL
};

const X509V3_EXT_METHOD v3_ocsp_acutoff = {
    NID_id_pkix_OCSP_archiveCutoff, 0, ASN1_ITEM_ref(ASN1_GENERALIZEDTIME),
    0, 0, 0, 0,
    0, 0,
    0, 0,
    i2r_ocsp_acutoff, 0,
    NULL
};

const X509V3_EXT_METHOD v3_crl_invdate = {
    NID_invalidity_date, 0, ASN1_ITEM_ref(ASN1_GENERALIZEDTIME),
    0, 0, 0, 0,
    0, 0,
    0, 0,
    i2r_ocsp_acutoff, 0,
    NULL
};

const X509V3_EXT_METHOD v3_crl_hold = {
    NID_hold_instruction_code, 0, ASN1_ITEM_ref(ASN1_OBJECT),
    0, 0, 0, 0,
    0, 0,
    0, 0,
    i2r_object, 0,
    NULL
};

const X509V3_EXT_METHOD v3_ocsp_nonce = {
    NID_id_pkix_OCSP_Nonce, 0, NULL,
    ocsp_nonce_new,
    ocsp_nonce_free,
    d2i_ocsp_nonce,
    i2d_ocsp_nonce,
    0, 0,
    0, 0,
    i2r_ocsp_nonce, 0,
    NULL
};

const X509V3_EXT_METHOD v3_ocsp_nocheck = {
    NID_id_pkix_OCSP_noCheck, 0, ASN1_ITEM_ref(ASN1_NULL),
    0, 0, 0, 0,
    0, s2i_ocsp_nocheck,
    0, 0,
    i2r_ocsp_nocheck, 0,
    NULL
};

const X509V3_EXT_METHOD v3_ocsp_serviceloc = {
    NID_id_pkix_OCSP_serviceLocator, 0, ASN1_ITEM_ref(OCSP_SERVICELOC),
    0, 0, 0, 0,
    0, 0,
    0, 0,
    i2r_ocsp_serviceloc, 0,
    NULL
};

static int i2r_ocsp_crlid(const X509V3_EXT_METHOD *method, void *in, BIO *bp,
                          int ind)
{
    OCSP_CRLID *a = in;
    if (a->crlUrl) {
        if (BIO_printf(bp, "%*scrlUrl: ", ind, "") <= 0)
            goto err;
        if (!ASN1_STRING_print(bp, (ASN1_STRING *)a->crlUrl))
            goto err;
        if (BIO_write(bp, "\n", 1) <= 0)
            goto err;
    }
    if (a->crlNum) {
        if (BIO_printf(bp, "%*scrlNum: ", ind, "") <= 0)
            goto err;
        if (i2a_ASN1_INTEGER(bp, a->crlNum) <= 0)
            goto err;
        if (BIO_write(bp, "\n", 1) <= 0)
            goto err;
    }
    if (a->crlTime) {
        if (BIO_printf(bp, "%*scrlTime: ", ind, "") <= 0)
            goto err;
        if (!ASN1_GENERALIZEDTIME_print(bp, a->crlTime))
            goto err;
        if (BIO_write(bp, "\n", 1) <= 0)
            goto err;
    }
    return 1;
 err:
    return 0;
}

static int i2r_ocsp_acutoff(const X509V3_EXT_METHOD *method, void *cutoff,
                            BIO *bp, int ind)
{
    if (BIO_printf(bp, "%*s", ind, "") <= 0)
        return 0;
    if (!ASN1_GENERALIZEDTIME_print(bp, cutoff))
        return 0;
    return 1;
}

static int i2r_object(const X509V3_EXT_METHOD *method, void *oid, BIO *bp,
                      int ind)
{
    if (BIO_printf(bp, "%*s", ind, "") <= 0)
        return 0;
    if (i2a_ASN1_OBJECT(bp, oid) <= 0)
        return 0;
    return 1;
}

/*
 * OCSP nonce. This is needs special treatment because it doesn't have an
 * ASN1 encoding at all: it just contains arbitrary data.
 */

static void *ocsp_nonce_new(void)
{
    return ASN1_OCTET_STRING_new();
}

static int i2d_ocsp_nonce(void *a, unsigned char **pp)
{
    ASN1_OCTET_STRING *os = a;
    if (pp) {
        memcpy(*pp, os->data, os->length);
        *pp += os->length;
    }
    return os->length;
}

static void *d2i_ocsp_nonce(void *a, const unsigned char **pp, long length)
{
    ASN1_OCTET_STRING *os, **pos;
    pos = a;
    if (pos == NULL || *pos == NULL) {
        os = ASN1_OCTET_STRING_new();
        if (os == NULL)
            goto err;
    } else {
        os = *pos;
    }
    if (!ASN1_OCTET_STRING_set(os, *pp, length))
        goto err;

    *pp += length;

    if (pos)
        *pos = os;
    return os;

 err:
    if ((pos == NULL) || (*pos != os))
        ASN1_OCTET_STRING_free(os);
    OCSPerr(OCSP_F_D2I_OCSP_NONCE, ERR_R_MALLOC_FAILURE);
    return NULL;
}

static void ocsp_nonce_free(void *a)
{
    ASN1_OCTET_STRING_free(a);
}

static int i2r_ocsp_nonce(const X509V3_EXT_METHOD *method, void *nonce,
                          BIO *out, int indent)
{
    if (BIO_printf(out, "%*s", indent, "") <= 0)
        return 0;
    if (i2a_ASN1_STRING(out, nonce, V_ASN1_OCTET_STRING) <= 0)
        return 0;
    return 1;
}

/* Nocheck is just a single NULL. Don't print anything and always set it */

static int i2r_ocsp_nocheck(const X509V3_EXT_METHOD *method, void *nocheck,
                            BIO *out, int indent)
{
    return 1;
}

static void *s2i_ocsp_nocheck(const X509V3_EXT_METHOD *method,
                              X509V3_CTX *ctx, const char *str)
{
    return ASN1_NULL_new();
}

static int i2r_ocsp_serviceloc(const X509V3_EXT_METHOD *method, void *in,
                               BIO *bp, int ind)
{
    int i;
    OCSP_SERVICELOC *a = in;
    ACCESS_DESCRIPTION *ad;

    if (BIO_printf(bp, "%*sIssuer: ", ind, "") <= 0)
        goto err;
    if (X509_NAME_print_ex(bp, a->issuer, 0, XN_FLAG_ONELINE) <= 0)
        goto err;
    for (i = 0; i < sk_ACCESS_DESCRIPTION_num(a->locator); i++) {
        ad = sk_ACCESS_DESCRIPTION_value(a->locator, i);
        if (BIO_printf(bp, "\n%*s", (2 * ind), "") <= 0)
            goto err;
        if (i2a_ASN1_OBJECT(bp, ad->method) <= 0)
            goto err;
        if (BIO_puts(bp, " - ") <= 0)
            goto err;
        if (GENERAL_NAME_print(bp, ad->location) <= 0)
            goto err;
    }
    return 1;
 err:
    return 0;
}
