/*
 * Copyright 2008-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/asn1t.h>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/cms.h>
#include "cms_lcl.h"

int CMS_stream(unsigned char ***boundary, CMS_ContentInfo *cms)
{
    ASN1_OCTET_STRING **pos;
    pos = CMS_get0_content(cms);
    if (pos == NULL)
        return 0;
    if (*pos == NULL)
        *pos = ASN1_OCTET_STRING_new();
    if (*pos != NULL) {
        (*pos)->flags |= ASN1_STRING_FLAG_NDEF;
        (*pos)->flags &= ~ASN1_STRING_FLAG_CONT;
        *boundary = &(*pos)->data;
        return 1;
    }
    CMSerr(CMS_F_CMS_STREAM, ERR_R_MALLOC_FAILURE);
    return 0;
}

CMS_ContentInfo *d2i_CMS_bio(BIO *bp, CMS_ContentInfo **cms)
{
    return ASN1_item_d2i_bio(ASN1_ITEM_rptr(CMS_ContentInfo), bp, cms);
}

int i2d_CMS_bio(BIO *bp, CMS_ContentInfo *cms)
{
    return ASN1_item_i2d_bio(ASN1_ITEM_rptr(CMS_ContentInfo), bp, cms);
}

IMPLEMENT_PEM_rw_const(CMS, CMS_ContentInfo, PEM_STRING_CMS, CMS_ContentInfo)

BIO *BIO_new_CMS(BIO *out, CMS_ContentInfo *cms)
{
    return BIO_new_NDEF(out, (ASN1_VALUE *)cms,
                        ASN1_ITEM_rptr(CMS_ContentInfo));
}

/* CMS wrappers round generalised stream and MIME routines */

int i2d_CMS_bio_stream(BIO *out, CMS_ContentInfo *cms, BIO *in, int flags)
{
    return i2d_ASN1_bio_stream(out, (ASN1_VALUE *)cms, in, flags,
                               ASN1_ITEM_rptr(CMS_ContentInfo));
}

int PEM_write_bio_CMS_stream(BIO *out, CMS_ContentInfo *cms, BIO *in,
                             int flags)
{
    return PEM_write_bio_ASN1_stream(out, (ASN1_VALUE *)cms, in, flags,
                                     "CMS", ASN1_ITEM_rptr(CMS_ContentInfo));
}

int SMIME_write_CMS(BIO *bio, CMS_ContentInfo *cms, BIO *data, int flags)
{
    STACK_OF(X509_ALGOR) *mdalgs;
    int ctype_nid = OBJ_obj2nid(cms->contentType);
    int econt_nid = OBJ_obj2nid(CMS_get0_eContentType(cms));
    if (ctype_nid == NID_pkcs7_signed)
        mdalgs = cms->d.signedData->digestAlgorithms;
    else
        mdalgs = NULL;

    return SMIME_write_ASN1(bio, (ASN1_VALUE *)cms, data, flags,
                            ctype_nid, econt_nid, mdalgs,
                            ASN1_ITEM_rptr(CMS_ContentInfo));
}

CMS_ContentInfo *SMIME_read_CMS(BIO *bio, BIO **bcont)
{
    return (CMS_ContentInfo *)SMIME_read_ASN1(bio, bcont,
                                              ASN1_ITEM_rptr
                                              (CMS_ContentInfo));
}
