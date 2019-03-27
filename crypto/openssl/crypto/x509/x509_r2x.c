/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/asn1.h>
#include <openssl/x509.h>
#include "internal/x509_int.h"
#include <openssl/objects.h>
#include <openssl/buffer.h>

X509 *X509_REQ_to_X509(X509_REQ *r, int days, EVP_PKEY *pkey)
{
    X509 *ret = NULL;
    X509_CINF *xi = NULL;
    X509_NAME *xn;
    EVP_PKEY *pubkey = NULL;

    if ((ret = X509_new()) == NULL) {
        X509err(X509_F_X509_REQ_TO_X509, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    /* duplicate the request */
    xi = &ret->cert_info;

    if (sk_X509_ATTRIBUTE_num(r->req_info.attributes) != 0) {
        if ((xi->version = ASN1_INTEGER_new()) == NULL)
            goto err;
        if (!ASN1_INTEGER_set(xi->version, 2))
            goto err;
/*-     xi->extensions=ri->attributes; <- bad, should not ever be done
        ri->attributes=NULL; */
    }

    xn = X509_REQ_get_subject_name(r);
    if (X509_set_subject_name(ret, xn) == 0)
        goto err;
    if (X509_set_issuer_name(ret, xn) == 0)
        goto err;

    if (X509_gmtime_adj(xi->validity.notBefore, 0) == NULL)
        goto err;
    if (X509_gmtime_adj(xi->validity.notAfter, (long)60 * 60 * 24 * days) ==
        NULL)
        goto err;

    pubkey = X509_REQ_get0_pubkey(r);
    if (pubkey == NULL || !X509_set_pubkey(ret, pubkey))
        goto err;

    if (!X509_sign(ret, pkey, EVP_md5()))
        goto err;
    return ret;

 err:
    X509_free(ret);
    return NULL;
}
