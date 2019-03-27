/*
 * Copyright 2008-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/cryptlib.h"
#include <openssl/asn1t.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <openssl/cms.h>
#include <openssl/bio.h>
#include <openssl/comp.h>
#include "cms_lcl.h"

#ifdef ZLIB

/* CMS CompressedData Utilities */

CMS_ContentInfo *cms_CompressedData_create(int comp_nid)
{
    CMS_ContentInfo *cms;
    CMS_CompressedData *cd;
    /*
     * Will need something cleverer if there is ever more than one
     * compression algorithm or parameters have some meaning...
     */
    if (comp_nid != NID_zlib_compression) {
        CMSerr(CMS_F_CMS_COMPRESSEDDATA_CREATE,
               CMS_R_UNSUPPORTED_COMPRESSION_ALGORITHM);
        return NULL;
    }
    cms = CMS_ContentInfo_new();
    if (cms == NULL)
        return NULL;

    cd = M_ASN1_new_of(CMS_CompressedData);

    if (cd == NULL)
        goto err;

    cms->contentType = OBJ_nid2obj(NID_id_smime_ct_compressedData);
    cms->d.compressedData = cd;

    cd->version = 0;

    X509_ALGOR_set0(cd->compressionAlgorithm,
                    OBJ_nid2obj(NID_zlib_compression), V_ASN1_UNDEF, NULL);

    cd->encapContentInfo->eContentType = OBJ_nid2obj(NID_pkcs7_data);

    return cms;

 err:
    CMS_ContentInfo_free(cms);
    return NULL;
}

BIO *cms_CompressedData_init_bio(CMS_ContentInfo *cms)
{
    CMS_CompressedData *cd;
    const ASN1_OBJECT *compoid;
    if (OBJ_obj2nid(cms->contentType) != NID_id_smime_ct_compressedData) {
        CMSerr(CMS_F_CMS_COMPRESSEDDATA_INIT_BIO,
               CMS_R_CONTENT_TYPE_NOT_COMPRESSED_DATA);
        return NULL;
    }
    cd = cms->d.compressedData;
    X509_ALGOR_get0(&compoid, NULL, NULL, cd->compressionAlgorithm);
    if (OBJ_obj2nid(compoid) != NID_zlib_compression) {
        CMSerr(CMS_F_CMS_COMPRESSEDDATA_INIT_BIO,
               CMS_R_UNSUPPORTED_COMPRESSION_ALGORITHM);
        return NULL;
    }
    return BIO_new(BIO_f_zlib());
}

#endif
