/*
 * Copyright 2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <time.h>
#include <errno.h>

#include "internal/cryptlib.h"
#include <openssl/asn1.h>
#include <openssl/x509.h>
#include <openssl/ossl_typ.h>
#include "x509_lcl.h"

X509_LOOKUP_METHOD *X509_LOOKUP_meth_new(const char *name)
{
    X509_LOOKUP_METHOD *method = OPENSSL_zalloc(sizeof(X509_LOOKUP_METHOD));

    if (method != NULL) {
        method->name = OPENSSL_strdup(name);
        if (method->name == NULL) {
            X509err(X509_F_X509_LOOKUP_METH_NEW, ERR_R_MALLOC_FAILURE);
            goto err;
        }
    }

    return method;

err:
    OPENSSL_free(method);
    return NULL;
}

void X509_LOOKUP_meth_free(X509_LOOKUP_METHOD *method)
{
    if (method != NULL)
        OPENSSL_free(method->name);
    OPENSSL_free(method);
}

int X509_LOOKUP_meth_set_new_item(X509_LOOKUP_METHOD *method,
                                  int (*new_item) (X509_LOOKUP *ctx))
{
    method->new_item = new_item;
    return 1;
}

int (*X509_LOOKUP_meth_get_new_item(const X509_LOOKUP_METHOD* method))
    (X509_LOOKUP *ctx)
{
    return method->new_item;
}

int X509_LOOKUP_meth_set_free(
    X509_LOOKUP_METHOD *method,
    void (*free_fn) (X509_LOOKUP *ctx))
{
    method->free = free_fn;
    return 1;
}

void (*X509_LOOKUP_meth_get_free(const X509_LOOKUP_METHOD* method))
    (X509_LOOKUP *ctx)
{
    return method->free;
}

int X509_LOOKUP_meth_set_init(X509_LOOKUP_METHOD *method,
                              int (*init) (X509_LOOKUP *ctx))
{
    method->init = init;
    return 1;
}

int (*X509_LOOKUP_meth_get_init(const X509_LOOKUP_METHOD* method))
    (X509_LOOKUP *ctx)
{
    return method->init;
}

int X509_LOOKUP_meth_set_shutdown(
    X509_LOOKUP_METHOD *method,
    int (*shutdown) (X509_LOOKUP *ctx))
{
    method->shutdown = shutdown;
    return 1;
}

int (*X509_LOOKUP_meth_get_shutdown(const X509_LOOKUP_METHOD* method))
    (X509_LOOKUP *ctx)
{
    return method->shutdown;
}

int X509_LOOKUP_meth_set_ctrl(
    X509_LOOKUP_METHOD *method,
    X509_LOOKUP_ctrl_fn ctrl)
{
    method->ctrl = ctrl;
    return 1;
}

X509_LOOKUP_ctrl_fn X509_LOOKUP_meth_get_ctrl(const X509_LOOKUP_METHOD *method)
{
    return method->ctrl;
}

int X509_LOOKUP_meth_set_get_by_subject(X509_LOOKUP_METHOD *method,
    X509_LOOKUP_get_by_subject_fn get_by_subject)
{
    method->get_by_subject = get_by_subject;
    return 1;
}

X509_LOOKUP_get_by_subject_fn X509_LOOKUP_meth_get_get_by_subject(
    const X509_LOOKUP_METHOD *method)
{
    return method->get_by_subject;
}


int X509_LOOKUP_meth_set_get_by_issuer_serial(X509_LOOKUP_METHOD *method,
    X509_LOOKUP_get_by_issuer_serial_fn get_by_issuer_serial)
{
    method->get_by_issuer_serial = get_by_issuer_serial;
    return 1;
}

X509_LOOKUP_get_by_issuer_serial_fn
    X509_LOOKUP_meth_get_get_by_issuer_serial(const X509_LOOKUP_METHOD *method)
{
    return method->get_by_issuer_serial;
}


int X509_LOOKUP_meth_set_get_by_fingerprint(X509_LOOKUP_METHOD *method,
    X509_LOOKUP_get_by_fingerprint_fn get_by_fingerprint)
{
    method->get_by_fingerprint = get_by_fingerprint;
    return 1;
}

X509_LOOKUP_get_by_fingerprint_fn X509_LOOKUP_meth_get_get_by_fingerprint(
    const X509_LOOKUP_METHOD *method)
{
    return method->get_by_fingerprint;
}

int X509_LOOKUP_meth_set_get_by_alias(X509_LOOKUP_METHOD *method,
                                      X509_LOOKUP_get_by_alias_fn get_by_alias)
{
    method->get_by_alias = get_by_alias;
    return 1;
}

X509_LOOKUP_get_by_alias_fn X509_LOOKUP_meth_get_get_by_alias(
    const X509_LOOKUP_METHOD *method)
{
    return method->get_by_alias;
}

