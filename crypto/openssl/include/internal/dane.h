/*
 * Copyright 2015-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_INTERNAL_DANE_H
#define HEADER_INTERNAL_DANE_H

#include <openssl/safestack.h>

/*-
 * Certificate usages:
 * https://tools.ietf.org/html/rfc6698#section-2.1.1
 */
#define DANETLS_USAGE_PKIX_TA   0
#define DANETLS_USAGE_PKIX_EE   1
#define DANETLS_USAGE_DANE_TA   2
#define DANETLS_USAGE_DANE_EE   3
#define DANETLS_USAGE_LAST      DANETLS_USAGE_DANE_EE

/*-
 * Selectors:
 * https://tools.ietf.org/html/rfc6698#section-2.1.2
 */
#define DANETLS_SELECTOR_CERT   0
#define DANETLS_SELECTOR_SPKI   1
#define DANETLS_SELECTOR_LAST   DANETLS_SELECTOR_SPKI

/*-
 * Matching types:
 * https://tools.ietf.org/html/rfc6698#section-2.1.3
 */
#define DANETLS_MATCHING_FULL   0
#define DANETLS_MATCHING_2256   1
#define DANETLS_MATCHING_2512   2
#define DANETLS_MATCHING_LAST   DANETLS_MATCHING_2512

typedef struct danetls_record_st {
    uint8_t usage;
    uint8_t selector;
    uint8_t mtype;
    unsigned char *data;
    size_t dlen;
    EVP_PKEY *spki;
} danetls_record;

DEFINE_STACK_OF(danetls_record)

/*
 * Shared DANE context
 */
struct dane_ctx_st {
    const EVP_MD  **mdevp;      /* mtype -> digest */
    uint8_t        *mdord;      /* mtype -> preference */
    uint8_t         mdmax;      /* highest supported mtype */
    unsigned long   flags;      /* feature bitmask */
};

/*
 * Per connection DANE state
 */
struct ssl_dane_st {
    struct dane_ctx_st *dctx;
    STACK_OF(danetls_record) *trecs;
    STACK_OF(X509) *certs;      /* DANE-TA(2) Cert(0) Full(0) certs */
    danetls_record *mtlsa;      /* Matching TLSA record */
    X509           *mcert;      /* DANE matched cert */
    uint32_t        umask;      /* Usages present */
    int             mdpth;      /* Depth of matched cert */
    int             pdpth;      /* Depth of PKIX trust */
    unsigned long   flags;      /* feature bitmask */
};

#define DANETLS_ENABLED(dane)  \
    ((dane) != NULL && sk_danetls_record_num((dane)->trecs) > 0)

#define DANETLS_USAGE_BIT(u)   (((uint32_t)1) << u)

#define DANETLS_PKIX_TA_MASK (DANETLS_USAGE_BIT(DANETLS_USAGE_PKIX_TA))
#define DANETLS_PKIX_EE_MASK (DANETLS_USAGE_BIT(DANETLS_USAGE_PKIX_EE))
#define DANETLS_DANE_TA_MASK (DANETLS_USAGE_BIT(DANETLS_USAGE_DANE_TA))
#define DANETLS_DANE_EE_MASK (DANETLS_USAGE_BIT(DANETLS_USAGE_DANE_EE))

#define DANETLS_PKIX_MASK (DANETLS_PKIX_TA_MASK | DANETLS_PKIX_EE_MASK)
#define DANETLS_DANE_MASK (DANETLS_DANE_TA_MASK | DANETLS_DANE_EE_MASK)
#define DANETLS_TA_MASK (DANETLS_PKIX_TA_MASK | DANETLS_DANE_TA_MASK)
#define DANETLS_EE_MASK (DANETLS_PKIX_EE_MASK | DANETLS_DANE_EE_MASK)

#define DANETLS_HAS_PKIX(dane) ((dane) && ((dane)->umask & DANETLS_PKIX_MASK))
#define DANETLS_HAS_DANE(dane) ((dane) && ((dane)->umask & DANETLS_DANE_MASK))
#define DANETLS_HAS_TA(dane)   ((dane) && ((dane)->umask & DANETLS_TA_MASK))
#define DANETLS_HAS_EE(dane)   ((dane) && ((dane)->umask & DANETLS_EE_MASK))

#define DANETLS_HAS_PKIX_TA(dane) ((dane)&&((dane)->umask & DANETLS_PKIX_TA_MASK))
#define DANETLS_HAS_PKIX_EE(dane) ((dane)&&((dane)->umask & DANETLS_PKIX_EE_MASK))
#define DANETLS_HAS_DANE_TA(dane) ((dane)&&((dane)->umask & DANETLS_DANE_TA_MASK))
#define DANETLS_HAS_DANE_EE(dane) ((dane)&&((dane)->umask & DANETLS_DANE_EE_MASK))

#endif /* HEADER_INTERNAL_DANE_H */
