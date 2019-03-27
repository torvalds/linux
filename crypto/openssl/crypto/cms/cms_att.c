/*
 * Copyright 2008-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/asn1t.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <openssl/cms.h>
#include "cms_lcl.h"

/* CMS SignedData Attribute utilities */

int CMS_signed_get_attr_count(const CMS_SignerInfo *si)
{
    return X509at_get_attr_count(si->signedAttrs);
}

int CMS_signed_get_attr_by_NID(const CMS_SignerInfo *si, int nid, int lastpos)
{
    return X509at_get_attr_by_NID(si->signedAttrs, nid, lastpos);
}

int CMS_signed_get_attr_by_OBJ(const CMS_SignerInfo *si, const ASN1_OBJECT *obj,
                               int lastpos)
{
    return X509at_get_attr_by_OBJ(si->signedAttrs, obj, lastpos);
}

X509_ATTRIBUTE *CMS_signed_get_attr(const CMS_SignerInfo *si, int loc)
{
    return X509at_get_attr(si->signedAttrs, loc);
}

X509_ATTRIBUTE *CMS_signed_delete_attr(CMS_SignerInfo *si, int loc)
{
    return X509at_delete_attr(si->signedAttrs, loc);
}

int CMS_signed_add1_attr(CMS_SignerInfo *si, X509_ATTRIBUTE *attr)
{
    if (X509at_add1_attr(&si->signedAttrs, attr))
        return 1;
    return 0;
}

int CMS_signed_add1_attr_by_OBJ(CMS_SignerInfo *si,
                                const ASN1_OBJECT *obj, int type,
                                const void *bytes, int len)
{
    if (X509at_add1_attr_by_OBJ(&si->signedAttrs, obj, type, bytes, len))
        return 1;
    return 0;
}

int CMS_signed_add1_attr_by_NID(CMS_SignerInfo *si,
                                int nid, int type, const void *bytes, int len)
{
    if (X509at_add1_attr_by_NID(&si->signedAttrs, nid, type, bytes, len))
        return 1;
    return 0;
}

int CMS_signed_add1_attr_by_txt(CMS_SignerInfo *si,
                                const char *attrname, int type,
                                const void *bytes, int len)
{
    if (X509at_add1_attr_by_txt(&si->signedAttrs, attrname, type, bytes, len))
        return 1;
    return 0;
}

void *CMS_signed_get0_data_by_OBJ(CMS_SignerInfo *si, const ASN1_OBJECT *oid,
                                  int lastpos, int type)
{
    return X509at_get0_data_by_OBJ(si->signedAttrs, oid, lastpos, type);
}

int CMS_unsigned_get_attr_count(const CMS_SignerInfo *si)
{
    return X509at_get_attr_count(si->unsignedAttrs);
}

int CMS_unsigned_get_attr_by_NID(const CMS_SignerInfo *si, int nid,
                                 int lastpos)
{
    return X509at_get_attr_by_NID(si->unsignedAttrs, nid, lastpos);
}

int CMS_unsigned_get_attr_by_OBJ(const CMS_SignerInfo *si,
                                 const ASN1_OBJECT *obj, int lastpos)
{
    return X509at_get_attr_by_OBJ(si->unsignedAttrs, obj, lastpos);
}

X509_ATTRIBUTE *CMS_unsigned_get_attr(const CMS_SignerInfo *si, int loc)
{
    return X509at_get_attr(si->unsignedAttrs, loc);
}

X509_ATTRIBUTE *CMS_unsigned_delete_attr(CMS_SignerInfo *si, int loc)
{
    return X509at_delete_attr(si->unsignedAttrs, loc);
}

int CMS_unsigned_add1_attr(CMS_SignerInfo *si, X509_ATTRIBUTE *attr)
{
    if (X509at_add1_attr(&si->unsignedAttrs, attr))
        return 1;
    return 0;
}

int CMS_unsigned_add1_attr_by_OBJ(CMS_SignerInfo *si,
                                  const ASN1_OBJECT *obj, int type,
                                  const void *bytes, int len)
{
    if (X509at_add1_attr_by_OBJ(&si->unsignedAttrs, obj, type, bytes, len))
        return 1;
    return 0;
}

int CMS_unsigned_add1_attr_by_NID(CMS_SignerInfo *si,
                                  int nid, int type,
                                  const void *bytes, int len)
{
    if (X509at_add1_attr_by_NID(&si->unsignedAttrs, nid, type, bytes, len))
        return 1;
    return 0;
}

int CMS_unsigned_add1_attr_by_txt(CMS_SignerInfo *si,
                                  const char *attrname, int type,
                                  const void *bytes, int len)
{
    if (X509at_add1_attr_by_txt(&si->unsignedAttrs, attrname,
                                type, bytes, len))
        return 1;
    return 0;
}

void *CMS_unsigned_get0_data_by_OBJ(CMS_SignerInfo *si, ASN1_OBJECT *oid,
                                    int lastpos, int type)
{
    return X509at_get0_data_by_OBJ(si->unsignedAttrs, oid, lastpos, type);
}

/* Specific attribute cases */
