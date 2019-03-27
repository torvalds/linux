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
#include <openssl/asn1t.h>
#include <openssl/x509.h>
#include "internal/x509_int.h"
#include <openssl/objects.h>
#include <openssl/buffer.h>
#include <openssl/pem.h>

X509_REQ *X509_to_X509_REQ(X509 *x, EVP_PKEY *pkey, const EVP_MD *md)
{
    X509_REQ *ret;
    X509_REQ_INFO *ri;
    int i;
    EVP_PKEY *pktmp;

    ret = X509_REQ_new();
    if (ret == NULL) {
        X509err(X509_F_X509_TO_X509_REQ, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    ri = &ret->req_info;

    ri->version->length = 1;
    ri->version->data = OPENSSL_malloc(1);
    if (ri->version->data == NULL)
        goto err;
    ri->version->data[0] = 0;   /* version == 0 */

    if (!X509_REQ_set_subject_name(ret, X509_get_subject_name(x)))
        goto err;

    pktmp = X509_get0_pubkey(x);
    if (pktmp == NULL)
        goto err;
    i = X509_REQ_set_pubkey(ret, pktmp);
    if (!i)
        goto err;

    if (pkey != NULL) {
        if (!X509_REQ_sign(ret, pkey, md))
            goto err;
    }
    return ret;
 err:
    X509_REQ_free(ret);
    return NULL;
}

EVP_PKEY *X509_REQ_get_pubkey(X509_REQ *req)
{
    if (req == NULL)
        return NULL;
    return X509_PUBKEY_get(req->req_info.pubkey);
}

EVP_PKEY *X509_REQ_get0_pubkey(X509_REQ *req)
{
    if (req == NULL)
        return NULL;
    return X509_PUBKEY_get0(req->req_info.pubkey);
}

X509_PUBKEY *X509_REQ_get_X509_PUBKEY(X509_REQ *req)
{
    return req->req_info.pubkey;
}

int X509_REQ_check_private_key(X509_REQ *x, EVP_PKEY *k)
{
    EVP_PKEY *xk = NULL;
    int ok = 0;

    xk = X509_REQ_get_pubkey(x);
    switch (EVP_PKEY_cmp(xk, k)) {
    case 1:
        ok = 1;
        break;
    case 0:
        X509err(X509_F_X509_REQ_CHECK_PRIVATE_KEY,
                X509_R_KEY_VALUES_MISMATCH);
        break;
    case -1:
        X509err(X509_F_X509_REQ_CHECK_PRIVATE_KEY, X509_R_KEY_TYPE_MISMATCH);
        break;
    case -2:
#ifndef OPENSSL_NO_EC
        if (EVP_PKEY_id(k) == EVP_PKEY_EC) {
            X509err(X509_F_X509_REQ_CHECK_PRIVATE_KEY, ERR_R_EC_LIB);
            break;
        }
#endif
#ifndef OPENSSL_NO_DH
        if (EVP_PKEY_id(k) == EVP_PKEY_DH) {
            /* No idea */
            X509err(X509_F_X509_REQ_CHECK_PRIVATE_KEY,
                    X509_R_CANT_CHECK_DH_KEY);
            break;
        }
#endif
        X509err(X509_F_X509_REQ_CHECK_PRIVATE_KEY, X509_R_UNKNOWN_KEY_TYPE);
    }

    EVP_PKEY_free(xk);
    return ok;
}

/*
 * It seems several organisations had the same idea of including a list of
 * extensions in a certificate request. There are at least two OIDs that are
 * used and there may be more: so the list is configurable.
 */

static int ext_nid_list[] = { NID_ext_req, NID_ms_ext_req, NID_undef };

static int *ext_nids = ext_nid_list;

int X509_REQ_extension_nid(int req_nid)
{
    int i, nid;
    for (i = 0;; i++) {
        nid = ext_nids[i];
        if (nid == NID_undef)
            return 0;
        else if (req_nid == nid)
            return 1;
    }
}

int *X509_REQ_get_extension_nids(void)
{
    return ext_nids;
}

void X509_REQ_set_extension_nids(int *nids)
{
    ext_nids = nids;
}

STACK_OF(X509_EXTENSION) *X509_REQ_get_extensions(X509_REQ *req)
{
    X509_ATTRIBUTE *attr;
    ASN1_TYPE *ext = NULL;
    int idx, *pnid;
    const unsigned char *p;

    if ((req == NULL) || !ext_nids)
        return NULL;
    for (pnid = ext_nids; *pnid != NID_undef; pnid++) {
        idx = X509_REQ_get_attr_by_NID(req, *pnid, -1);
        if (idx == -1)
            continue;
        attr = X509_REQ_get_attr(req, idx);
        ext = X509_ATTRIBUTE_get0_type(attr, 0);
        break;
    }
    if (!ext || (ext->type != V_ASN1_SEQUENCE))
        return NULL;
    p = ext->value.sequence->data;
    return (STACK_OF(X509_EXTENSION) *)
        ASN1_item_d2i(NULL, &p, ext->value.sequence->length,
                      ASN1_ITEM_rptr(X509_EXTENSIONS));
}

/*
 * Add a STACK_OF extensions to a certificate request: allow alternative OIDs
 * in case we want to create a non standard one.
 */

int X509_REQ_add_extensions_nid(X509_REQ *req, STACK_OF(X509_EXTENSION) *exts,
                                int nid)
{
    int extlen;
    int rv = 0;
    unsigned char *ext = NULL;
    /* Generate encoding of extensions */
    extlen = ASN1_item_i2d((ASN1_VALUE *)exts, &ext,
                           ASN1_ITEM_rptr(X509_EXTENSIONS));
    if (extlen <= 0)
        return 0;
    rv = X509_REQ_add1_attr_by_NID(req, nid, V_ASN1_SEQUENCE, ext, extlen);
    OPENSSL_free(ext);
    return rv;
}

/* This is the normal usage: use the "official" OID */
int X509_REQ_add_extensions(X509_REQ *req, STACK_OF(X509_EXTENSION) *exts)
{
    return X509_REQ_add_extensions_nid(req, exts, NID_ext_req);
}

/* Request attribute functions */

int X509_REQ_get_attr_count(const X509_REQ *req)
{
    return X509at_get_attr_count(req->req_info.attributes);
}

int X509_REQ_get_attr_by_NID(const X509_REQ *req, int nid, int lastpos)
{
    return X509at_get_attr_by_NID(req->req_info.attributes, nid, lastpos);
}

int X509_REQ_get_attr_by_OBJ(const X509_REQ *req, const ASN1_OBJECT *obj,
                             int lastpos)
{
    return X509at_get_attr_by_OBJ(req->req_info.attributes, obj, lastpos);
}

X509_ATTRIBUTE *X509_REQ_get_attr(const X509_REQ *req, int loc)
{
    return X509at_get_attr(req->req_info.attributes, loc);
}

X509_ATTRIBUTE *X509_REQ_delete_attr(X509_REQ *req, int loc)
{
    return X509at_delete_attr(req->req_info.attributes, loc);
}

int X509_REQ_add1_attr(X509_REQ *req, X509_ATTRIBUTE *attr)
{
    if (X509at_add1_attr(&req->req_info.attributes, attr))
        return 1;
    return 0;
}

int X509_REQ_add1_attr_by_OBJ(X509_REQ *req,
                              const ASN1_OBJECT *obj, int type,
                              const unsigned char *bytes, int len)
{
    if (X509at_add1_attr_by_OBJ(&req->req_info.attributes, obj,
                                type, bytes, len))
        return 1;
    return 0;
}

int X509_REQ_add1_attr_by_NID(X509_REQ *req,
                              int nid, int type,
                              const unsigned char *bytes, int len)
{
    if (X509at_add1_attr_by_NID(&req->req_info.attributes, nid,
                                type, bytes, len))
        return 1;
    return 0;
}

int X509_REQ_add1_attr_by_txt(X509_REQ *req,
                              const char *attrname, int type,
                              const unsigned char *bytes, int len)
{
    if (X509at_add1_attr_by_txt(&req->req_info.attributes, attrname,
                                type, bytes, len))
        return 1;
    return 0;
}

long X509_REQ_get_version(const X509_REQ *req)
{
    return ASN1_INTEGER_get(req->req_info.version);
}

X509_NAME *X509_REQ_get_subject_name(const X509_REQ *req)
{
    return req->req_info.subject;
}

void X509_REQ_get0_signature(const X509_REQ *req, const ASN1_BIT_STRING **psig,
                             const X509_ALGOR **palg)
{
    if (psig != NULL)
        *psig = req->signature;
    if (palg != NULL)
        *palg = &req->sig_alg;
}

int X509_REQ_get_signature_nid(const X509_REQ *req)
{
    return OBJ_obj2nid(req->sig_alg.algorithm);
}

int i2d_re_X509_REQ_tbs(X509_REQ *req, unsigned char **pp)
{
    req->req_info.enc.modified = 1;
    return i2d_X509_REQ_INFO(&req->req_info, pp);
}
