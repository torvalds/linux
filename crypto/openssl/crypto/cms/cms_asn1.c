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
#include <openssl/cms.h>
#include "cms_lcl.h"


ASN1_SEQUENCE(CMS_IssuerAndSerialNumber) = {
        ASN1_SIMPLE(CMS_IssuerAndSerialNumber, issuer, X509_NAME),
        ASN1_SIMPLE(CMS_IssuerAndSerialNumber, serialNumber, ASN1_INTEGER)
} ASN1_SEQUENCE_END(CMS_IssuerAndSerialNumber)

ASN1_SEQUENCE(CMS_OtherCertificateFormat) = {
        ASN1_SIMPLE(CMS_OtherCertificateFormat, otherCertFormat, ASN1_OBJECT),
        ASN1_OPT(CMS_OtherCertificateFormat, otherCert, ASN1_ANY)
} static_ASN1_SEQUENCE_END(CMS_OtherCertificateFormat)

ASN1_CHOICE(CMS_CertificateChoices) = {
        ASN1_SIMPLE(CMS_CertificateChoices, d.certificate, X509),
        ASN1_IMP(CMS_CertificateChoices, d.extendedCertificate, ASN1_SEQUENCE, 0),
        ASN1_IMP(CMS_CertificateChoices, d.v1AttrCert, ASN1_SEQUENCE, 1),
        ASN1_IMP(CMS_CertificateChoices, d.v2AttrCert, ASN1_SEQUENCE, 2),
        ASN1_IMP(CMS_CertificateChoices, d.other, CMS_OtherCertificateFormat, 3)
} ASN1_CHOICE_END(CMS_CertificateChoices)

ASN1_CHOICE(CMS_SignerIdentifier) = {
        ASN1_SIMPLE(CMS_SignerIdentifier, d.issuerAndSerialNumber, CMS_IssuerAndSerialNumber),
        ASN1_IMP(CMS_SignerIdentifier, d.subjectKeyIdentifier, ASN1_OCTET_STRING, 0)
} static_ASN1_CHOICE_END(CMS_SignerIdentifier)

ASN1_NDEF_SEQUENCE(CMS_EncapsulatedContentInfo) = {
        ASN1_SIMPLE(CMS_EncapsulatedContentInfo, eContentType, ASN1_OBJECT),
        ASN1_NDEF_EXP_OPT(CMS_EncapsulatedContentInfo, eContent, ASN1_OCTET_STRING_NDEF, 0)
} static_ASN1_NDEF_SEQUENCE_END(CMS_EncapsulatedContentInfo)

/* Minor tweak to operation: free up signer key, cert */
static int cms_si_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it,
                     void *exarg)
{
    if (operation == ASN1_OP_FREE_POST) {
        CMS_SignerInfo *si = (CMS_SignerInfo *)*pval;
        EVP_PKEY_free(si->pkey);
        X509_free(si->signer);
        EVP_MD_CTX_free(si->mctx);
    }
    return 1;
}

ASN1_SEQUENCE_cb(CMS_SignerInfo, cms_si_cb) = {
        ASN1_EMBED(CMS_SignerInfo, version, INT32),
        ASN1_SIMPLE(CMS_SignerInfo, sid, CMS_SignerIdentifier),
        ASN1_SIMPLE(CMS_SignerInfo, digestAlgorithm, X509_ALGOR),
        ASN1_IMP_SET_OF_OPT(CMS_SignerInfo, signedAttrs, X509_ATTRIBUTE, 0),
        ASN1_SIMPLE(CMS_SignerInfo, signatureAlgorithm, X509_ALGOR),
        ASN1_SIMPLE(CMS_SignerInfo, signature, ASN1_OCTET_STRING),
        ASN1_IMP_SET_OF_OPT(CMS_SignerInfo, unsignedAttrs, X509_ATTRIBUTE, 1)
} ASN1_SEQUENCE_END_cb(CMS_SignerInfo, CMS_SignerInfo)

ASN1_SEQUENCE(CMS_OtherRevocationInfoFormat) = {
        ASN1_SIMPLE(CMS_OtherRevocationInfoFormat, otherRevInfoFormat, ASN1_OBJECT),
        ASN1_OPT(CMS_OtherRevocationInfoFormat, otherRevInfo, ASN1_ANY)
} static_ASN1_SEQUENCE_END(CMS_OtherRevocationInfoFormat)

ASN1_CHOICE(CMS_RevocationInfoChoice) = {
        ASN1_SIMPLE(CMS_RevocationInfoChoice, d.crl, X509_CRL),
        ASN1_IMP(CMS_RevocationInfoChoice, d.other, CMS_OtherRevocationInfoFormat, 1)
} ASN1_CHOICE_END(CMS_RevocationInfoChoice)

ASN1_NDEF_SEQUENCE(CMS_SignedData) = {
        ASN1_EMBED(CMS_SignedData, version, INT32),
        ASN1_SET_OF(CMS_SignedData, digestAlgorithms, X509_ALGOR),
        ASN1_SIMPLE(CMS_SignedData, encapContentInfo, CMS_EncapsulatedContentInfo),
        ASN1_IMP_SET_OF_OPT(CMS_SignedData, certificates, CMS_CertificateChoices, 0),
        ASN1_IMP_SET_OF_OPT(CMS_SignedData, crls, CMS_RevocationInfoChoice, 1),
        ASN1_SET_OF(CMS_SignedData, signerInfos, CMS_SignerInfo)
} ASN1_NDEF_SEQUENCE_END(CMS_SignedData)

ASN1_SEQUENCE(CMS_OriginatorInfo) = {
        ASN1_IMP_SET_OF_OPT(CMS_OriginatorInfo, certificates, CMS_CertificateChoices, 0),
        ASN1_IMP_SET_OF_OPT(CMS_OriginatorInfo, crls, CMS_RevocationInfoChoice, 1)
} static_ASN1_SEQUENCE_END(CMS_OriginatorInfo)

ASN1_NDEF_SEQUENCE(CMS_EncryptedContentInfo) = {
        ASN1_SIMPLE(CMS_EncryptedContentInfo, contentType, ASN1_OBJECT),
        ASN1_SIMPLE(CMS_EncryptedContentInfo, contentEncryptionAlgorithm, X509_ALGOR),
        ASN1_IMP_OPT(CMS_EncryptedContentInfo, encryptedContent, ASN1_OCTET_STRING_NDEF, 0)
} static_ASN1_NDEF_SEQUENCE_END(CMS_EncryptedContentInfo)

ASN1_SEQUENCE(CMS_KeyTransRecipientInfo) = {
        ASN1_EMBED(CMS_KeyTransRecipientInfo, version, INT32),
        ASN1_SIMPLE(CMS_KeyTransRecipientInfo, rid, CMS_SignerIdentifier),
        ASN1_SIMPLE(CMS_KeyTransRecipientInfo, keyEncryptionAlgorithm, X509_ALGOR),
        ASN1_SIMPLE(CMS_KeyTransRecipientInfo, encryptedKey, ASN1_OCTET_STRING)
} ASN1_SEQUENCE_END(CMS_KeyTransRecipientInfo)

ASN1_SEQUENCE(CMS_OtherKeyAttribute) = {
        ASN1_SIMPLE(CMS_OtherKeyAttribute, keyAttrId, ASN1_OBJECT),
        ASN1_OPT(CMS_OtherKeyAttribute, keyAttr, ASN1_ANY)
} ASN1_SEQUENCE_END(CMS_OtherKeyAttribute)

ASN1_SEQUENCE(CMS_RecipientKeyIdentifier) = {
        ASN1_SIMPLE(CMS_RecipientKeyIdentifier, subjectKeyIdentifier, ASN1_OCTET_STRING),
        ASN1_OPT(CMS_RecipientKeyIdentifier, date, ASN1_GENERALIZEDTIME),
        ASN1_OPT(CMS_RecipientKeyIdentifier, other, CMS_OtherKeyAttribute)
} ASN1_SEQUENCE_END(CMS_RecipientKeyIdentifier)

ASN1_CHOICE(CMS_KeyAgreeRecipientIdentifier) = {
  ASN1_SIMPLE(CMS_KeyAgreeRecipientIdentifier, d.issuerAndSerialNumber, CMS_IssuerAndSerialNumber),
  ASN1_IMP(CMS_KeyAgreeRecipientIdentifier, d.rKeyId, CMS_RecipientKeyIdentifier, 0)
} static_ASN1_CHOICE_END(CMS_KeyAgreeRecipientIdentifier)

static int cms_rek_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it,
                      void *exarg)
{
    CMS_RecipientEncryptedKey *rek = (CMS_RecipientEncryptedKey *)*pval;
    if (operation == ASN1_OP_FREE_POST) {
        EVP_PKEY_free(rek->pkey);
    }
    return 1;
}

ASN1_SEQUENCE_cb(CMS_RecipientEncryptedKey, cms_rek_cb) = {
        ASN1_SIMPLE(CMS_RecipientEncryptedKey, rid, CMS_KeyAgreeRecipientIdentifier),
        ASN1_SIMPLE(CMS_RecipientEncryptedKey, encryptedKey, ASN1_OCTET_STRING)
} ASN1_SEQUENCE_END_cb(CMS_RecipientEncryptedKey, CMS_RecipientEncryptedKey)

ASN1_SEQUENCE(CMS_OriginatorPublicKey) = {
  ASN1_SIMPLE(CMS_OriginatorPublicKey, algorithm, X509_ALGOR),
  ASN1_SIMPLE(CMS_OriginatorPublicKey, publicKey, ASN1_BIT_STRING)
} ASN1_SEQUENCE_END(CMS_OriginatorPublicKey)

ASN1_CHOICE(CMS_OriginatorIdentifierOrKey) = {
  ASN1_SIMPLE(CMS_OriginatorIdentifierOrKey, d.issuerAndSerialNumber, CMS_IssuerAndSerialNumber),
  ASN1_IMP(CMS_OriginatorIdentifierOrKey, d.subjectKeyIdentifier, ASN1_OCTET_STRING, 0),
  ASN1_IMP(CMS_OriginatorIdentifierOrKey, d.originatorKey, CMS_OriginatorPublicKey, 1)
} static_ASN1_CHOICE_END(CMS_OriginatorIdentifierOrKey)

static int cms_kari_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it,
                       void *exarg)
{
    CMS_KeyAgreeRecipientInfo *kari = (CMS_KeyAgreeRecipientInfo *)*pval;
    if (operation == ASN1_OP_NEW_POST) {
        kari->ctx = EVP_CIPHER_CTX_new();
        if (kari->ctx == NULL)
            return 0;
        EVP_CIPHER_CTX_set_flags(kari->ctx, EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);
        kari->pctx = NULL;
    } else if (operation == ASN1_OP_FREE_POST) {
        EVP_PKEY_CTX_free(kari->pctx);
        EVP_CIPHER_CTX_free(kari->ctx);
    }
    return 1;
}

ASN1_SEQUENCE_cb(CMS_KeyAgreeRecipientInfo, cms_kari_cb) = {
        ASN1_EMBED(CMS_KeyAgreeRecipientInfo, version, INT32),
        ASN1_EXP(CMS_KeyAgreeRecipientInfo, originator, CMS_OriginatorIdentifierOrKey, 0),
        ASN1_EXP_OPT(CMS_KeyAgreeRecipientInfo, ukm, ASN1_OCTET_STRING, 1),
        ASN1_SIMPLE(CMS_KeyAgreeRecipientInfo, keyEncryptionAlgorithm, X509_ALGOR),
        ASN1_SEQUENCE_OF(CMS_KeyAgreeRecipientInfo, recipientEncryptedKeys, CMS_RecipientEncryptedKey)
} ASN1_SEQUENCE_END_cb(CMS_KeyAgreeRecipientInfo, CMS_KeyAgreeRecipientInfo)

ASN1_SEQUENCE(CMS_KEKIdentifier) = {
        ASN1_SIMPLE(CMS_KEKIdentifier, keyIdentifier, ASN1_OCTET_STRING),
        ASN1_OPT(CMS_KEKIdentifier, date, ASN1_GENERALIZEDTIME),
        ASN1_OPT(CMS_KEKIdentifier, other, CMS_OtherKeyAttribute)
} static_ASN1_SEQUENCE_END(CMS_KEKIdentifier)

ASN1_SEQUENCE(CMS_KEKRecipientInfo) = {
        ASN1_EMBED(CMS_KEKRecipientInfo, version, INT32),
        ASN1_SIMPLE(CMS_KEKRecipientInfo, kekid, CMS_KEKIdentifier),
        ASN1_SIMPLE(CMS_KEKRecipientInfo, keyEncryptionAlgorithm, X509_ALGOR),
        ASN1_SIMPLE(CMS_KEKRecipientInfo, encryptedKey, ASN1_OCTET_STRING)
} ASN1_SEQUENCE_END(CMS_KEKRecipientInfo)

ASN1_SEQUENCE(CMS_PasswordRecipientInfo) = {
        ASN1_EMBED(CMS_PasswordRecipientInfo, version, INT32),
        ASN1_IMP_OPT(CMS_PasswordRecipientInfo, keyDerivationAlgorithm, X509_ALGOR, 0),
        ASN1_SIMPLE(CMS_PasswordRecipientInfo, keyEncryptionAlgorithm, X509_ALGOR),
        ASN1_SIMPLE(CMS_PasswordRecipientInfo, encryptedKey, ASN1_OCTET_STRING)
} ASN1_SEQUENCE_END(CMS_PasswordRecipientInfo)

ASN1_SEQUENCE(CMS_OtherRecipientInfo) = {
  ASN1_SIMPLE(CMS_OtherRecipientInfo, oriType, ASN1_OBJECT),
  ASN1_OPT(CMS_OtherRecipientInfo, oriValue, ASN1_ANY)
} static_ASN1_SEQUENCE_END(CMS_OtherRecipientInfo)

/* Free up RecipientInfo additional data */
static int cms_ri_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it,
                     void *exarg)
{
    if (operation == ASN1_OP_FREE_PRE) {
        CMS_RecipientInfo *ri = (CMS_RecipientInfo *)*pval;
        if (ri->type == CMS_RECIPINFO_TRANS) {
            CMS_KeyTransRecipientInfo *ktri = ri->d.ktri;
            EVP_PKEY_free(ktri->pkey);
            X509_free(ktri->recip);
            EVP_PKEY_CTX_free(ktri->pctx);
        } else if (ri->type == CMS_RECIPINFO_KEK) {
            CMS_KEKRecipientInfo *kekri = ri->d.kekri;
            OPENSSL_clear_free(kekri->key, kekri->keylen);
        } else if (ri->type == CMS_RECIPINFO_PASS) {
            CMS_PasswordRecipientInfo *pwri = ri->d.pwri;
            OPENSSL_clear_free(pwri->pass, pwri->passlen);
        }
    }
    return 1;
}

ASN1_CHOICE_cb(CMS_RecipientInfo, cms_ri_cb) = {
        ASN1_SIMPLE(CMS_RecipientInfo, d.ktri, CMS_KeyTransRecipientInfo),
        ASN1_IMP(CMS_RecipientInfo, d.kari, CMS_KeyAgreeRecipientInfo, 1),
        ASN1_IMP(CMS_RecipientInfo, d.kekri, CMS_KEKRecipientInfo, 2),
        ASN1_IMP(CMS_RecipientInfo, d.pwri, CMS_PasswordRecipientInfo, 3),
        ASN1_IMP(CMS_RecipientInfo, d.ori, CMS_OtherRecipientInfo, 4)
} ASN1_CHOICE_END_cb(CMS_RecipientInfo, CMS_RecipientInfo, type)

ASN1_NDEF_SEQUENCE(CMS_EnvelopedData) = {
        ASN1_EMBED(CMS_EnvelopedData, version, INT32),
        ASN1_IMP_OPT(CMS_EnvelopedData, originatorInfo, CMS_OriginatorInfo, 0),
        ASN1_SET_OF(CMS_EnvelopedData, recipientInfos, CMS_RecipientInfo),
        ASN1_SIMPLE(CMS_EnvelopedData, encryptedContentInfo, CMS_EncryptedContentInfo),
        ASN1_IMP_SET_OF_OPT(CMS_EnvelopedData, unprotectedAttrs, X509_ATTRIBUTE, 1)
} ASN1_NDEF_SEQUENCE_END(CMS_EnvelopedData)

ASN1_NDEF_SEQUENCE(CMS_DigestedData) = {
        ASN1_EMBED(CMS_DigestedData, version, INT32),
        ASN1_SIMPLE(CMS_DigestedData, digestAlgorithm, X509_ALGOR),
        ASN1_SIMPLE(CMS_DigestedData, encapContentInfo, CMS_EncapsulatedContentInfo),
        ASN1_SIMPLE(CMS_DigestedData, digest, ASN1_OCTET_STRING)
} ASN1_NDEF_SEQUENCE_END(CMS_DigestedData)

ASN1_NDEF_SEQUENCE(CMS_EncryptedData) = {
        ASN1_EMBED(CMS_EncryptedData, version, INT32),
        ASN1_SIMPLE(CMS_EncryptedData, encryptedContentInfo, CMS_EncryptedContentInfo),
        ASN1_IMP_SET_OF_OPT(CMS_EncryptedData, unprotectedAttrs, X509_ATTRIBUTE, 1)
} ASN1_NDEF_SEQUENCE_END(CMS_EncryptedData)

ASN1_NDEF_SEQUENCE(CMS_AuthenticatedData) = {
        ASN1_EMBED(CMS_AuthenticatedData, version, INT32),
        ASN1_IMP_OPT(CMS_AuthenticatedData, originatorInfo, CMS_OriginatorInfo, 0),
        ASN1_SET_OF(CMS_AuthenticatedData, recipientInfos, CMS_RecipientInfo),
        ASN1_SIMPLE(CMS_AuthenticatedData, macAlgorithm, X509_ALGOR),
        ASN1_IMP(CMS_AuthenticatedData, digestAlgorithm, X509_ALGOR, 1),
        ASN1_SIMPLE(CMS_AuthenticatedData, encapContentInfo, CMS_EncapsulatedContentInfo),
        ASN1_IMP_SET_OF_OPT(CMS_AuthenticatedData, authAttrs, X509_ALGOR, 2),
        ASN1_SIMPLE(CMS_AuthenticatedData, mac, ASN1_OCTET_STRING),
        ASN1_IMP_SET_OF_OPT(CMS_AuthenticatedData, unauthAttrs, X509_ALGOR, 3)
} static_ASN1_NDEF_SEQUENCE_END(CMS_AuthenticatedData)

ASN1_NDEF_SEQUENCE(CMS_CompressedData) = {
        ASN1_EMBED(CMS_CompressedData, version, INT32),
        ASN1_SIMPLE(CMS_CompressedData, compressionAlgorithm, X509_ALGOR),
        ASN1_SIMPLE(CMS_CompressedData, encapContentInfo, CMS_EncapsulatedContentInfo),
} ASN1_NDEF_SEQUENCE_END(CMS_CompressedData)

/* This is the ANY DEFINED BY table for the top level ContentInfo structure */

ASN1_ADB_TEMPLATE(cms_default) = ASN1_EXP(CMS_ContentInfo, d.other, ASN1_ANY, 0);

ASN1_ADB(CMS_ContentInfo) = {
        ADB_ENTRY(NID_pkcs7_data, ASN1_NDEF_EXP(CMS_ContentInfo, d.data, ASN1_OCTET_STRING_NDEF, 0)),
        ADB_ENTRY(NID_pkcs7_signed, ASN1_NDEF_EXP(CMS_ContentInfo, d.signedData, CMS_SignedData, 0)),
        ADB_ENTRY(NID_pkcs7_enveloped, ASN1_NDEF_EXP(CMS_ContentInfo, d.envelopedData, CMS_EnvelopedData, 0)),
        ADB_ENTRY(NID_pkcs7_digest, ASN1_NDEF_EXP(CMS_ContentInfo, d.digestedData, CMS_DigestedData, 0)),
        ADB_ENTRY(NID_pkcs7_encrypted, ASN1_NDEF_EXP(CMS_ContentInfo, d.encryptedData, CMS_EncryptedData, 0)),
        ADB_ENTRY(NID_id_smime_ct_authData, ASN1_NDEF_EXP(CMS_ContentInfo, d.authenticatedData, CMS_AuthenticatedData, 0)),
        ADB_ENTRY(NID_id_smime_ct_compressedData, ASN1_NDEF_EXP(CMS_ContentInfo, d.compressedData, CMS_CompressedData, 0)),
} ASN1_ADB_END(CMS_ContentInfo, 0, contentType, 0, &cms_default_tt, NULL);

/* CMS streaming support */
static int cms_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it,
                  void *exarg)
{
    ASN1_STREAM_ARG *sarg = exarg;
    CMS_ContentInfo *cms = NULL;
    if (pval)
        cms = (CMS_ContentInfo *)*pval;
    else
        return 1;
    switch (operation) {

    case ASN1_OP_STREAM_PRE:
        if (CMS_stream(&sarg->boundary, cms) <= 0)
            return 0;
        /* fall thru */
    case ASN1_OP_DETACHED_PRE:
        sarg->ndef_bio = CMS_dataInit(cms, sarg->out);
        if (!sarg->ndef_bio)
            return 0;
        break;

    case ASN1_OP_STREAM_POST:
    case ASN1_OP_DETACHED_POST:
        if (CMS_dataFinal(cms, sarg->ndef_bio) <= 0)
            return 0;
        break;

    }
    return 1;
}

ASN1_NDEF_SEQUENCE_cb(CMS_ContentInfo, cms_cb) = {
        ASN1_SIMPLE(CMS_ContentInfo, contentType, ASN1_OBJECT),
        ASN1_ADB_OBJECT(CMS_ContentInfo)
} ASN1_NDEF_SEQUENCE_END_cb(CMS_ContentInfo, CMS_ContentInfo)

/* Specials for signed attributes */

/*
 * When signing attributes we want to reorder them to match the sorted
 * encoding.
 */

ASN1_ITEM_TEMPLATE(CMS_Attributes_Sign) =
        ASN1_EX_TEMPLATE_TYPE(ASN1_TFLG_SET_ORDER, 0, CMS_ATTRIBUTES, X509_ATTRIBUTE)
ASN1_ITEM_TEMPLATE_END(CMS_Attributes_Sign)

/*
 * When verifying attributes we need to use the received order. So we use
 * SEQUENCE OF and tag it to SET OF
 */

ASN1_ITEM_TEMPLATE(CMS_Attributes_Verify) =
        ASN1_EX_TEMPLATE_TYPE(ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_IMPTAG | ASN1_TFLG_UNIVERSAL,
                                V_ASN1_SET, CMS_ATTRIBUTES, X509_ATTRIBUTE)
ASN1_ITEM_TEMPLATE_END(CMS_Attributes_Verify)



ASN1_CHOICE(CMS_ReceiptsFrom) = {
  ASN1_IMP_EMBED(CMS_ReceiptsFrom, d.allOrFirstTier, INT32, 0),
  ASN1_IMP_SEQUENCE_OF(CMS_ReceiptsFrom, d.receiptList, GENERAL_NAMES, 1)
} static_ASN1_CHOICE_END(CMS_ReceiptsFrom)

ASN1_SEQUENCE(CMS_ReceiptRequest) = {
  ASN1_SIMPLE(CMS_ReceiptRequest, signedContentIdentifier, ASN1_OCTET_STRING),
  ASN1_SIMPLE(CMS_ReceiptRequest, receiptsFrom, CMS_ReceiptsFrom),
  ASN1_SEQUENCE_OF(CMS_ReceiptRequest, receiptsTo, GENERAL_NAMES)
} ASN1_SEQUENCE_END(CMS_ReceiptRequest)

ASN1_SEQUENCE(CMS_Receipt) = {
  ASN1_EMBED(CMS_Receipt, version, INT32),
  ASN1_SIMPLE(CMS_Receipt, contentType, ASN1_OBJECT),
  ASN1_SIMPLE(CMS_Receipt, signedContentIdentifier, ASN1_OCTET_STRING),
  ASN1_SIMPLE(CMS_Receipt, originatorSignatureValue, ASN1_OCTET_STRING)
} ASN1_SEQUENCE_END(CMS_Receipt)

/*
 * Utilities to encode the CMS_SharedInfo structure used during key
 * derivation.
 */

typedef struct {
    X509_ALGOR *keyInfo;
    ASN1_OCTET_STRING *entityUInfo;
    ASN1_OCTET_STRING *suppPubInfo;
} CMS_SharedInfo;

ASN1_SEQUENCE(CMS_SharedInfo) = {
  ASN1_SIMPLE(CMS_SharedInfo, keyInfo, X509_ALGOR),
  ASN1_EXP_OPT(CMS_SharedInfo, entityUInfo, ASN1_OCTET_STRING, 0),
  ASN1_EXP_OPT(CMS_SharedInfo, suppPubInfo, ASN1_OCTET_STRING, 2),
} static_ASN1_SEQUENCE_END(CMS_SharedInfo)

int CMS_SharedInfo_encode(unsigned char **pder, X509_ALGOR *kekalg,
                          ASN1_OCTET_STRING *ukm, int keylen)
{
    union {
        CMS_SharedInfo *pecsi;
        ASN1_VALUE *a;
    } intsi = {
        NULL
    };

    ASN1_OCTET_STRING oklen;
    unsigned char kl[4];
    CMS_SharedInfo ecsi;

    keylen <<= 3;
    kl[0] = (keylen >> 24) & 0xff;
    kl[1] = (keylen >> 16) & 0xff;
    kl[2] = (keylen >> 8) & 0xff;
    kl[3] = keylen & 0xff;
    oklen.length = 4;
    oklen.data = kl;
    oklen.type = V_ASN1_OCTET_STRING;
    oklen.flags = 0;
    ecsi.keyInfo = kekalg;
    ecsi.entityUInfo = ukm;
    ecsi.suppPubInfo = &oklen;
    intsi.pecsi = &ecsi;
    return ASN1_item_i2d(intsi.a, pder, ASN1_ITEM_rptr(CMS_SharedInfo));
}
