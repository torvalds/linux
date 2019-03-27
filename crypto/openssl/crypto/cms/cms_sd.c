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
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <openssl/cms.h>
#include "cms_lcl.h"
#include "internal/asn1_int.h"
#include "internal/evp_int.h"

/* CMS SignedData Utilities */

static CMS_SignedData *cms_get0_signed(CMS_ContentInfo *cms)
{
    if (OBJ_obj2nid(cms->contentType) != NID_pkcs7_signed) {
        CMSerr(CMS_F_CMS_GET0_SIGNED, CMS_R_CONTENT_TYPE_NOT_SIGNED_DATA);
        return NULL;
    }
    return cms->d.signedData;
}

static CMS_SignedData *cms_signed_data_init(CMS_ContentInfo *cms)
{
    if (cms->d.other == NULL) {
        cms->d.signedData = M_ASN1_new_of(CMS_SignedData);
        if (!cms->d.signedData) {
            CMSerr(CMS_F_CMS_SIGNED_DATA_INIT, ERR_R_MALLOC_FAILURE);
            return NULL;
        }
        cms->d.signedData->version = 1;
        cms->d.signedData->encapContentInfo->eContentType =
            OBJ_nid2obj(NID_pkcs7_data);
        cms->d.signedData->encapContentInfo->partial = 1;
        ASN1_OBJECT_free(cms->contentType);
        cms->contentType = OBJ_nid2obj(NID_pkcs7_signed);
        return cms->d.signedData;
    }
    return cms_get0_signed(cms);
}

/* Just initialise SignedData e.g. for certs only structure */

int CMS_SignedData_init(CMS_ContentInfo *cms)
{
    if (cms_signed_data_init(cms))
        return 1;
    else
        return 0;
}

/* Check structures and fixup version numbers (if necessary) */

static void cms_sd_set_version(CMS_SignedData *sd)
{
    int i;
    CMS_CertificateChoices *cch;
    CMS_RevocationInfoChoice *rch;
    CMS_SignerInfo *si;

    for (i = 0; i < sk_CMS_CertificateChoices_num(sd->certificates); i++) {
        cch = sk_CMS_CertificateChoices_value(sd->certificates, i);
        if (cch->type == CMS_CERTCHOICE_OTHER) {
            if (sd->version < 5)
                sd->version = 5;
        } else if (cch->type == CMS_CERTCHOICE_V2ACERT) {
            if (sd->version < 4)
                sd->version = 4;
        } else if (cch->type == CMS_CERTCHOICE_V1ACERT) {
            if (sd->version < 3)
                sd->version = 3;
        }
    }

    for (i = 0; i < sk_CMS_RevocationInfoChoice_num(sd->crls); i++) {
        rch = sk_CMS_RevocationInfoChoice_value(sd->crls, i);
        if (rch->type == CMS_REVCHOICE_OTHER) {
            if (sd->version < 5)
                sd->version = 5;
        }
    }

    if ((OBJ_obj2nid(sd->encapContentInfo->eContentType) != NID_pkcs7_data)
        && (sd->version < 3))
        sd->version = 3;

    for (i = 0; i < sk_CMS_SignerInfo_num(sd->signerInfos); i++) {
        si = sk_CMS_SignerInfo_value(sd->signerInfos, i);
        if (si->sid->type == CMS_SIGNERINFO_KEYIDENTIFIER) {
            if (si->version < 3)
                si->version = 3;
            if (sd->version < 3)
                sd->version = 3;
        } else if (si->version < 1)
            si->version = 1;
    }

    if (sd->version < 1)
        sd->version = 1;

}

/* Copy an existing messageDigest value */

static int cms_copy_messageDigest(CMS_ContentInfo *cms, CMS_SignerInfo *si)
{
    STACK_OF(CMS_SignerInfo) *sinfos;
    CMS_SignerInfo *sitmp;
    int i;
    sinfos = CMS_get0_SignerInfos(cms);
    for (i = 0; i < sk_CMS_SignerInfo_num(sinfos); i++) {
        ASN1_OCTET_STRING *messageDigest;
        sitmp = sk_CMS_SignerInfo_value(sinfos, i);
        if (sitmp == si)
            continue;
        if (CMS_signed_get_attr_count(sitmp) < 0)
            continue;
        if (OBJ_cmp(si->digestAlgorithm->algorithm,
                    sitmp->digestAlgorithm->algorithm))
            continue;
        messageDigest = CMS_signed_get0_data_by_OBJ(sitmp,
                                                    OBJ_nid2obj
                                                    (NID_pkcs9_messageDigest),
                                                    -3, V_ASN1_OCTET_STRING);
        if (!messageDigest) {
            CMSerr(CMS_F_CMS_COPY_MESSAGEDIGEST,
                   CMS_R_ERROR_READING_MESSAGEDIGEST_ATTRIBUTE);
            return 0;
        }

        if (CMS_signed_add1_attr_by_NID(si, NID_pkcs9_messageDigest,
                                        V_ASN1_OCTET_STRING,
                                        messageDigest, -1))
            return 1;
        else
            return 0;
    }
    CMSerr(CMS_F_CMS_COPY_MESSAGEDIGEST, CMS_R_NO_MATCHING_DIGEST);
    return 0;
}

int cms_set1_SignerIdentifier(CMS_SignerIdentifier *sid, X509 *cert, int type)
{
    switch (type) {
    case CMS_SIGNERINFO_ISSUER_SERIAL:
        if (!cms_set1_ias(&sid->d.issuerAndSerialNumber, cert))
            return 0;
        break;

    case CMS_SIGNERINFO_KEYIDENTIFIER:
        if (!cms_set1_keyid(&sid->d.subjectKeyIdentifier, cert))
            return 0;
        break;

    default:
        CMSerr(CMS_F_CMS_SET1_SIGNERIDENTIFIER, CMS_R_UNKNOWN_ID);
        return 0;
    }

    sid->type = type;

    return 1;
}

int cms_SignerIdentifier_get0_signer_id(CMS_SignerIdentifier *sid,
                                        ASN1_OCTET_STRING **keyid,
                                        X509_NAME **issuer,
                                        ASN1_INTEGER **sno)
{
    if (sid->type == CMS_SIGNERINFO_ISSUER_SERIAL) {
        if (issuer)
            *issuer = sid->d.issuerAndSerialNumber->issuer;
        if (sno)
            *sno = sid->d.issuerAndSerialNumber->serialNumber;
    } else if (sid->type == CMS_SIGNERINFO_KEYIDENTIFIER) {
        if (keyid)
            *keyid = sid->d.subjectKeyIdentifier;
    } else
        return 0;
    return 1;
}

int cms_SignerIdentifier_cert_cmp(CMS_SignerIdentifier *sid, X509 *cert)
{
    if (sid->type == CMS_SIGNERINFO_ISSUER_SERIAL)
        return cms_ias_cert_cmp(sid->d.issuerAndSerialNumber, cert);
    else if (sid->type == CMS_SIGNERINFO_KEYIDENTIFIER)
        return cms_keyid_cert_cmp(sid->d.subjectKeyIdentifier, cert);
    else
        return -1;
}

static int cms_sd_asn1_ctrl(CMS_SignerInfo *si, int cmd)
{
    EVP_PKEY *pkey = si->pkey;
    int i;
    if (!pkey->ameth || !pkey->ameth->pkey_ctrl)
        return 1;
    i = pkey->ameth->pkey_ctrl(pkey, ASN1_PKEY_CTRL_CMS_SIGN, cmd, si);
    if (i == -2) {
        CMSerr(CMS_F_CMS_SD_ASN1_CTRL, CMS_R_NOT_SUPPORTED_FOR_THIS_KEY_TYPE);
        return 0;
    }
    if (i <= 0) {
        CMSerr(CMS_F_CMS_SD_ASN1_CTRL, CMS_R_CTRL_FAILURE);
        return 0;
    }
    return 1;
}

CMS_SignerInfo *CMS_add1_signer(CMS_ContentInfo *cms,
                                X509 *signer, EVP_PKEY *pk, const EVP_MD *md,
                                unsigned int flags)
{
    CMS_SignedData *sd;
    CMS_SignerInfo *si = NULL;
    X509_ALGOR *alg;
    int i, type;
    if (!X509_check_private_key(signer, pk)) {
        CMSerr(CMS_F_CMS_ADD1_SIGNER,
               CMS_R_PRIVATE_KEY_DOES_NOT_MATCH_CERTIFICATE);
        return NULL;
    }
    sd = cms_signed_data_init(cms);
    if (!sd)
        goto err;
    si = M_ASN1_new_of(CMS_SignerInfo);
    if (!si)
        goto merr;
    /* Call for side-effect of computing hash and caching extensions */
    X509_check_purpose(signer, -1, -1);

    X509_up_ref(signer);
    EVP_PKEY_up_ref(pk);

    si->pkey = pk;
    si->signer = signer;
    si->mctx = EVP_MD_CTX_new();
    si->pctx = NULL;

    if (si->mctx == NULL) {
        CMSerr(CMS_F_CMS_ADD1_SIGNER, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    if (flags & CMS_USE_KEYID) {
        si->version = 3;
        if (sd->version < 3)
            sd->version = 3;
        type = CMS_SIGNERINFO_KEYIDENTIFIER;
    } else {
        type = CMS_SIGNERINFO_ISSUER_SERIAL;
        si->version = 1;
    }

    if (!cms_set1_SignerIdentifier(si->sid, signer, type))
        goto err;

    if (md == NULL) {
        int def_nid;
        if (EVP_PKEY_get_default_digest_nid(pk, &def_nid) <= 0)
            goto err;
        md = EVP_get_digestbynid(def_nid);
        if (md == NULL) {
            CMSerr(CMS_F_CMS_ADD1_SIGNER, CMS_R_NO_DEFAULT_DIGEST);
            goto err;
        }
    }

    if (!md) {
        CMSerr(CMS_F_CMS_ADD1_SIGNER, CMS_R_NO_DIGEST_SET);
        goto err;
    }

    X509_ALGOR_set_md(si->digestAlgorithm, md);

    /* See if digest is present in digestAlgorithms */
    for (i = 0; i < sk_X509_ALGOR_num(sd->digestAlgorithms); i++) {
        const ASN1_OBJECT *aoid;
        alg = sk_X509_ALGOR_value(sd->digestAlgorithms, i);
        X509_ALGOR_get0(&aoid, NULL, NULL, alg);
        if (OBJ_obj2nid(aoid) == EVP_MD_type(md))
            break;
    }

    if (i == sk_X509_ALGOR_num(sd->digestAlgorithms)) {
        alg = X509_ALGOR_new();
        if (alg == NULL)
            goto merr;
        X509_ALGOR_set_md(alg, md);
        if (!sk_X509_ALGOR_push(sd->digestAlgorithms, alg)) {
            X509_ALGOR_free(alg);
            goto merr;
        }
    }

    if (!(flags & CMS_KEY_PARAM) && !cms_sd_asn1_ctrl(si, 0))
        goto err;
    if (!(flags & CMS_NOATTR)) {
        /*
         * Initialize signed attributes structure so other attributes
         * such as signing time etc are added later even if we add none here.
         */
        if (!si->signedAttrs) {
            si->signedAttrs = sk_X509_ATTRIBUTE_new_null();
            if (!si->signedAttrs)
                goto merr;
        }

        if (!(flags & CMS_NOSMIMECAP)) {
            STACK_OF(X509_ALGOR) *smcap = NULL;
            i = CMS_add_standard_smimecap(&smcap);
            if (i)
                i = CMS_add_smimecap(si, smcap);
            sk_X509_ALGOR_pop_free(smcap, X509_ALGOR_free);
            if (!i)
                goto merr;
        }
        if (flags & CMS_REUSE_DIGEST) {
            if (!cms_copy_messageDigest(cms, si))
                goto err;
            if (!(flags & (CMS_PARTIAL | CMS_KEY_PARAM)) &&
                !CMS_SignerInfo_sign(si))
                goto err;
        }
    }

    if (!(flags & CMS_NOCERTS)) {
        /* NB ignore -1 return for duplicate cert */
        if (!CMS_add1_cert(cms, signer))
            goto merr;
    }

    if (flags & CMS_KEY_PARAM) {
        if (flags & CMS_NOATTR) {
            si->pctx = EVP_PKEY_CTX_new(si->pkey, NULL);
            if (si->pctx == NULL)
                goto err;
            if (EVP_PKEY_sign_init(si->pctx) <= 0)
                goto err;
            if (EVP_PKEY_CTX_set_signature_md(si->pctx, md) <= 0)
                goto err;
        } else if (EVP_DigestSignInit(si->mctx, &si->pctx, md, NULL, pk) <=
                   0)
            goto err;
    }

    if (!sd->signerInfos)
        sd->signerInfos = sk_CMS_SignerInfo_new_null();
    if (!sd->signerInfos || !sk_CMS_SignerInfo_push(sd->signerInfos, si))
        goto merr;

    return si;

 merr:
    CMSerr(CMS_F_CMS_ADD1_SIGNER, ERR_R_MALLOC_FAILURE);
 err:
    M_ASN1_free_of(si, CMS_SignerInfo);
    return NULL;

}

static int cms_add1_signingTime(CMS_SignerInfo *si, ASN1_TIME *t)
{
    ASN1_TIME *tt;
    int r = 0;
    if (t)
        tt = t;
    else
        tt = X509_gmtime_adj(NULL, 0);

    if (!tt)
        goto merr;

    if (CMS_signed_add1_attr_by_NID(si, NID_pkcs9_signingTime,
                                    tt->type, tt, -1) <= 0)
        goto merr;

    r = 1;

 merr:

    if (!t)
        ASN1_TIME_free(tt);

    if (!r)
        CMSerr(CMS_F_CMS_ADD1_SIGNINGTIME, ERR_R_MALLOC_FAILURE);

    return r;

}

EVP_PKEY_CTX *CMS_SignerInfo_get0_pkey_ctx(CMS_SignerInfo *si)
{
    return si->pctx;
}

EVP_MD_CTX *CMS_SignerInfo_get0_md_ctx(CMS_SignerInfo *si)
{
    return si->mctx;
}

STACK_OF(CMS_SignerInfo) *CMS_get0_SignerInfos(CMS_ContentInfo *cms)
{
    CMS_SignedData *sd;
    sd = cms_get0_signed(cms);
    if (!sd)
        return NULL;
    return sd->signerInfos;
}

STACK_OF(X509) *CMS_get0_signers(CMS_ContentInfo *cms)
{
    STACK_OF(X509) *signers = NULL;
    STACK_OF(CMS_SignerInfo) *sinfos;
    CMS_SignerInfo *si;
    int i;
    sinfos = CMS_get0_SignerInfos(cms);
    for (i = 0; i < sk_CMS_SignerInfo_num(sinfos); i++) {
        si = sk_CMS_SignerInfo_value(sinfos, i);
        if (si->signer) {
            if (!signers) {
                signers = sk_X509_new_null();
                if (!signers)
                    return NULL;
            }
            if (!sk_X509_push(signers, si->signer)) {
                sk_X509_free(signers);
                return NULL;
            }
        }
    }
    return signers;
}

void CMS_SignerInfo_set1_signer_cert(CMS_SignerInfo *si, X509 *signer)
{
    if (signer) {
        X509_up_ref(signer);
        EVP_PKEY_free(si->pkey);
        si->pkey = X509_get_pubkey(signer);
    }
    X509_free(si->signer);
    si->signer = signer;
}

int CMS_SignerInfo_get0_signer_id(CMS_SignerInfo *si,
                                  ASN1_OCTET_STRING **keyid,
                                  X509_NAME **issuer, ASN1_INTEGER **sno)
{
    return cms_SignerIdentifier_get0_signer_id(si->sid, keyid, issuer, sno);
}

int CMS_SignerInfo_cert_cmp(CMS_SignerInfo *si, X509 *cert)
{
    return cms_SignerIdentifier_cert_cmp(si->sid, cert);
}

int CMS_set1_signers_certs(CMS_ContentInfo *cms, STACK_OF(X509) *scerts,
                           unsigned int flags)
{
    CMS_SignedData *sd;
    CMS_SignerInfo *si;
    CMS_CertificateChoices *cch;
    STACK_OF(CMS_CertificateChoices) *certs;
    X509 *x;
    int i, j;
    int ret = 0;
    sd = cms_get0_signed(cms);
    if (!sd)
        return -1;
    certs = sd->certificates;
    for (i = 0; i < sk_CMS_SignerInfo_num(sd->signerInfos); i++) {
        si = sk_CMS_SignerInfo_value(sd->signerInfos, i);
        if (si->signer)
            continue;

        for (j = 0; j < sk_X509_num(scerts); j++) {
            x = sk_X509_value(scerts, j);
            if (CMS_SignerInfo_cert_cmp(si, x) == 0) {
                CMS_SignerInfo_set1_signer_cert(si, x);
                ret++;
                break;
            }
        }

        if (si->signer || (flags & CMS_NOINTERN))
            continue;

        for (j = 0; j < sk_CMS_CertificateChoices_num(certs); j++) {
            cch = sk_CMS_CertificateChoices_value(certs, j);
            if (cch->type != 0)
                continue;
            x = cch->d.certificate;
            if (CMS_SignerInfo_cert_cmp(si, x) == 0) {
                CMS_SignerInfo_set1_signer_cert(si, x);
                ret++;
                break;
            }
        }
    }
    return ret;
}

void CMS_SignerInfo_get0_algs(CMS_SignerInfo *si, EVP_PKEY **pk,
                              X509 **signer, X509_ALGOR **pdig,
                              X509_ALGOR **psig)
{
    if (pk)
        *pk = si->pkey;
    if (signer)
        *signer = si->signer;
    if (pdig)
        *pdig = si->digestAlgorithm;
    if (psig)
        *psig = si->signatureAlgorithm;
}

ASN1_OCTET_STRING *CMS_SignerInfo_get0_signature(CMS_SignerInfo *si)
{
    return si->signature;
}

static int cms_SignerInfo_content_sign(CMS_ContentInfo *cms,
                                       CMS_SignerInfo *si, BIO *chain)
{
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    int r = 0;
    EVP_PKEY_CTX *pctx = NULL;

    if (mctx == NULL) {
        CMSerr(CMS_F_CMS_SIGNERINFO_CONTENT_SIGN, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    if (!si->pkey) {
        CMSerr(CMS_F_CMS_SIGNERINFO_CONTENT_SIGN, CMS_R_NO_PRIVATE_KEY);
        goto err;
    }

    if (!cms_DigestAlgorithm_find_ctx(mctx, chain, si->digestAlgorithm))
        goto err;
    /* Set SignerInfo algorithm details if we used custom parameter */
    if (si->pctx && !cms_sd_asn1_ctrl(si, 0))
        goto err;

    /*
     * If any signed attributes calculate and add messageDigest attribute
     */

    if (CMS_signed_get_attr_count(si) >= 0) {
        ASN1_OBJECT *ctype =
            cms->d.signedData->encapContentInfo->eContentType;
        unsigned char md[EVP_MAX_MD_SIZE];
        unsigned int mdlen;
        if (!EVP_DigestFinal_ex(mctx, md, &mdlen))
            goto err;
        if (!CMS_signed_add1_attr_by_NID(si, NID_pkcs9_messageDigest,
                                         V_ASN1_OCTET_STRING, md, mdlen))
            goto err;
        /* Copy content type across */
        if (CMS_signed_add1_attr_by_NID(si, NID_pkcs9_contentType,
                                        V_ASN1_OBJECT, ctype, -1) <= 0)
            goto err;
        if (!CMS_SignerInfo_sign(si))
            goto err;
    } else if (si->pctx) {
        unsigned char *sig;
        size_t siglen;
        unsigned char md[EVP_MAX_MD_SIZE];
        unsigned int mdlen;
        pctx = si->pctx;
        if (!EVP_DigestFinal_ex(mctx, md, &mdlen))
            goto err;
        siglen = EVP_PKEY_size(si->pkey);
        sig = OPENSSL_malloc(siglen);
        if (sig == NULL) {
            CMSerr(CMS_F_CMS_SIGNERINFO_CONTENT_SIGN, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        if (EVP_PKEY_sign(pctx, sig, &siglen, md, mdlen) <= 0) {
            OPENSSL_free(sig);
            goto err;
        }
        ASN1_STRING_set0(si->signature, sig, siglen);
    } else {
        unsigned char *sig;
        unsigned int siglen;
        sig = OPENSSL_malloc(EVP_PKEY_size(si->pkey));
        if (sig == NULL) {
            CMSerr(CMS_F_CMS_SIGNERINFO_CONTENT_SIGN, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        if (!EVP_SignFinal(mctx, sig, &siglen, si->pkey)) {
            CMSerr(CMS_F_CMS_SIGNERINFO_CONTENT_SIGN, CMS_R_SIGNFINAL_ERROR);
            OPENSSL_free(sig);
            goto err;
        }
        ASN1_STRING_set0(si->signature, sig, siglen);
    }

    r = 1;

 err:
    EVP_MD_CTX_free(mctx);
    EVP_PKEY_CTX_free(pctx);
    return r;

}

int cms_SignedData_final(CMS_ContentInfo *cms, BIO *chain)
{
    STACK_OF(CMS_SignerInfo) *sinfos;
    CMS_SignerInfo *si;
    int i;
    sinfos = CMS_get0_SignerInfos(cms);
    for (i = 0; i < sk_CMS_SignerInfo_num(sinfos); i++) {
        si = sk_CMS_SignerInfo_value(sinfos, i);
        if (!cms_SignerInfo_content_sign(cms, si, chain))
            return 0;
    }
    cms->d.signedData->encapContentInfo->partial = 0;
    return 1;
}

int CMS_SignerInfo_sign(CMS_SignerInfo *si)
{
    EVP_MD_CTX *mctx = si->mctx;
    EVP_PKEY_CTX *pctx = NULL;
    unsigned char *abuf = NULL;
    int alen;
    size_t siglen;
    const EVP_MD *md = NULL;

    md = EVP_get_digestbyobj(si->digestAlgorithm->algorithm);
    if (md == NULL)
        return 0;

    if (CMS_signed_get_attr_by_NID(si, NID_pkcs9_signingTime, -1) < 0) {
        if (!cms_add1_signingTime(si, NULL))
            goto err;
    }

    if (si->pctx)
        pctx = si->pctx;
    else {
        EVP_MD_CTX_reset(mctx);
        if (EVP_DigestSignInit(mctx, &pctx, md, NULL, si->pkey) <= 0)
            goto err;
        si->pctx = pctx;
    }

    if (EVP_PKEY_CTX_ctrl(pctx, -1, EVP_PKEY_OP_SIGN,
                          EVP_PKEY_CTRL_CMS_SIGN, 0, si) <= 0) {
        CMSerr(CMS_F_CMS_SIGNERINFO_SIGN, CMS_R_CTRL_ERROR);
        goto err;
    }

    alen = ASN1_item_i2d((ASN1_VALUE *)si->signedAttrs, &abuf,
                         ASN1_ITEM_rptr(CMS_Attributes_Sign));
    if (!abuf)
        goto err;
    if (EVP_DigestSignUpdate(mctx, abuf, alen) <= 0)
        goto err;
    if (EVP_DigestSignFinal(mctx, NULL, &siglen) <= 0)
        goto err;
    OPENSSL_free(abuf);
    abuf = OPENSSL_malloc(siglen);
    if (abuf == NULL)
        goto err;
    if (EVP_DigestSignFinal(mctx, abuf, &siglen) <= 0)
        goto err;

    if (EVP_PKEY_CTX_ctrl(pctx, -1, EVP_PKEY_OP_SIGN,
                          EVP_PKEY_CTRL_CMS_SIGN, 1, si) <= 0) {
        CMSerr(CMS_F_CMS_SIGNERINFO_SIGN, CMS_R_CTRL_ERROR);
        goto err;
    }

    EVP_MD_CTX_reset(mctx);

    ASN1_STRING_set0(si->signature, abuf, siglen);

    return 1;

 err:
    OPENSSL_free(abuf);
    EVP_MD_CTX_reset(mctx);
    return 0;

}

int CMS_SignerInfo_verify(CMS_SignerInfo *si)
{
    EVP_MD_CTX *mctx = NULL;
    unsigned char *abuf = NULL;
    int alen, r = -1;
    const EVP_MD *md = NULL;

    if (!si->pkey) {
        CMSerr(CMS_F_CMS_SIGNERINFO_VERIFY, CMS_R_NO_PUBLIC_KEY);
        return -1;
    }

    md = EVP_get_digestbyobj(si->digestAlgorithm->algorithm);
    if (md == NULL)
        return -1;
    if (si->mctx == NULL && (si->mctx = EVP_MD_CTX_new()) == NULL) {
        CMSerr(CMS_F_CMS_SIGNERINFO_VERIFY, ERR_R_MALLOC_FAILURE);
        return -1;
    }
    mctx = si->mctx;
    if (EVP_DigestVerifyInit(mctx, &si->pctx, md, NULL, si->pkey) <= 0)
        goto err;

    if (!cms_sd_asn1_ctrl(si, 1))
        goto err;

    alen = ASN1_item_i2d((ASN1_VALUE *)si->signedAttrs, &abuf,
                         ASN1_ITEM_rptr(CMS_Attributes_Verify));
    if (!abuf)
        goto err;
    r = EVP_DigestVerifyUpdate(mctx, abuf, alen);
    OPENSSL_free(abuf);
    if (r <= 0) {
        r = -1;
        goto err;
    }
    r = EVP_DigestVerifyFinal(mctx,
                              si->signature->data, si->signature->length);
    if (r <= 0)
        CMSerr(CMS_F_CMS_SIGNERINFO_VERIFY, CMS_R_VERIFICATION_FAILURE);
 err:
    EVP_MD_CTX_reset(mctx);
    return r;
}

/* Create a chain of digest BIOs from a CMS ContentInfo */

BIO *cms_SignedData_init_bio(CMS_ContentInfo *cms)
{
    int i;
    CMS_SignedData *sd;
    BIO *chain = NULL;
    sd = cms_get0_signed(cms);
    if (!sd)
        return NULL;
    if (cms->d.signedData->encapContentInfo->partial)
        cms_sd_set_version(sd);
    for (i = 0; i < sk_X509_ALGOR_num(sd->digestAlgorithms); i++) {
        X509_ALGOR *digestAlgorithm;
        BIO *mdbio;
        digestAlgorithm = sk_X509_ALGOR_value(sd->digestAlgorithms, i);
        mdbio = cms_DigestAlgorithm_init_bio(digestAlgorithm);
        if (!mdbio)
            goto err;
        if (chain)
            BIO_push(chain, mdbio);
        else
            chain = mdbio;
    }
    return chain;
 err:
    BIO_free_all(chain);
    return NULL;
}

int CMS_SignerInfo_verify_content(CMS_SignerInfo *si, BIO *chain)
{
    ASN1_OCTET_STRING *os = NULL;
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    EVP_PKEY_CTX *pkctx = NULL;
    int r = -1;
    unsigned char mval[EVP_MAX_MD_SIZE];
    unsigned int mlen;

    if (mctx == NULL) {
        CMSerr(CMS_F_CMS_SIGNERINFO_VERIFY_CONTENT, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    /* If we have any signed attributes look for messageDigest value */
    if (CMS_signed_get_attr_count(si) >= 0) {
        os = CMS_signed_get0_data_by_OBJ(si,
                                         OBJ_nid2obj(NID_pkcs9_messageDigest),
                                         -3, V_ASN1_OCTET_STRING);
        if (!os) {
            CMSerr(CMS_F_CMS_SIGNERINFO_VERIFY_CONTENT,
                   CMS_R_ERROR_READING_MESSAGEDIGEST_ATTRIBUTE);
            goto err;
        }
    }

    if (!cms_DigestAlgorithm_find_ctx(mctx, chain, si->digestAlgorithm))
        goto err;

    if (EVP_DigestFinal_ex(mctx, mval, &mlen) <= 0) {
        CMSerr(CMS_F_CMS_SIGNERINFO_VERIFY_CONTENT,
               CMS_R_UNABLE_TO_FINALIZE_CONTEXT);
        goto err;
    }

    /* If messageDigest found compare it */

    if (os) {
        if (mlen != (unsigned int)os->length) {
            CMSerr(CMS_F_CMS_SIGNERINFO_VERIFY_CONTENT,
                   CMS_R_MESSAGEDIGEST_ATTRIBUTE_WRONG_LENGTH);
            goto err;
        }

        if (memcmp(mval, os->data, mlen)) {
            CMSerr(CMS_F_CMS_SIGNERINFO_VERIFY_CONTENT,
                   CMS_R_VERIFICATION_FAILURE);
            r = 0;
        } else
            r = 1;
    } else {
        const EVP_MD *md = EVP_MD_CTX_md(mctx);
        pkctx = EVP_PKEY_CTX_new(si->pkey, NULL);
        if (pkctx == NULL)
            goto err;
        if (EVP_PKEY_verify_init(pkctx) <= 0)
            goto err;
        if (EVP_PKEY_CTX_set_signature_md(pkctx, md) <= 0)
            goto err;
        si->pctx = pkctx;
        if (!cms_sd_asn1_ctrl(si, 1))
            goto err;
        r = EVP_PKEY_verify(pkctx, si->signature->data,
                            si->signature->length, mval, mlen);
        if (r <= 0) {
            CMSerr(CMS_F_CMS_SIGNERINFO_VERIFY_CONTENT,
                   CMS_R_VERIFICATION_FAILURE);
            r = 0;
        }
    }

 err:
    EVP_PKEY_CTX_free(pkctx);
    EVP_MD_CTX_free(mctx);
    return r;

}

int CMS_add_smimecap(CMS_SignerInfo *si, STACK_OF(X509_ALGOR) *algs)
{
    unsigned char *smder = NULL;
    int smderlen, r;
    smderlen = i2d_X509_ALGORS(algs, &smder);
    if (smderlen <= 0)
        return 0;
    r = CMS_signed_add1_attr_by_NID(si, NID_SMIMECapabilities,
                                    V_ASN1_SEQUENCE, smder, smderlen);
    OPENSSL_free(smder);
    return r;
}

int CMS_add_simple_smimecap(STACK_OF(X509_ALGOR) **algs,
                            int algnid, int keysize)
{
    X509_ALGOR *alg;
    ASN1_INTEGER *key = NULL;
    if (keysize > 0) {
        key = ASN1_INTEGER_new();
        if (key == NULL || !ASN1_INTEGER_set(key, keysize))
            return 0;
    }
    alg = X509_ALGOR_new();
    if (alg == NULL) {
        ASN1_INTEGER_free(key);
        return 0;
    }

    X509_ALGOR_set0(alg, OBJ_nid2obj(algnid),
                    key ? V_ASN1_INTEGER : V_ASN1_UNDEF, key);
    if (*algs == NULL)
        *algs = sk_X509_ALGOR_new_null();
    if (*algs == NULL || !sk_X509_ALGOR_push(*algs, alg)) {
        X509_ALGOR_free(alg);
        return 0;
    }
    return 1;
}

/* Check to see if a cipher exists and if so add S/MIME capabilities */

static int cms_add_cipher_smcap(STACK_OF(X509_ALGOR) **sk, int nid, int arg)
{
    if (EVP_get_cipherbynid(nid))
        return CMS_add_simple_smimecap(sk, nid, arg);
    return 1;
}

static int cms_add_digest_smcap(STACK_OF(X509_ALGOR) **sk, int nid, int arg)
{
    if (EVP_get_digestbynid(nid))
        return CMS_add_simple_smimecap(sk, nid, arg);
    return 1;
}

int CMS_add_standard_smimecap(STACK_OF(X509_ALGOR) **smcap)
{
    if (!cms_add_cipher_smcap(smcap, NID_aes_256_cbc, -1)
        || !cms_add_digest_smcap(smcap, NID_id_GostR3411_2012_256, -1)
        || !cms_add_digest_smcap(smcap, NID_id_GostR3411_2012_512, -1)
        || !cms_add_digest_smcap(smcap, NID_id_GostR3411_94, -1)
        || !cms_add_cipher_smcap(smcap, NID_id_Gost28147_89, -1)
        || !cms_add_cipher_smcap(smcap, NID_aes_192_cbc, -1)
        || !cms_add_cipher_smcap(smcap, NID_aes_128_cbc, -1)
        || !cms_add_cipher_smcap(smcap, NID_des_ede3_cbc, -1)
        || !cms_add_cipher_smcap(smcap, NID_rc2_cbc, 128)
        || !cms_add_cipher_smcap(smcap, NID_rc2_cbc, 64)
        || !cms_add_cipher_smcap(smcap, NID_des_cbc, -1)
        || !cms_add_cipher_smcap(smcap, NID_rc2_cbc, 40))
        return 0;
    return 1;
}
