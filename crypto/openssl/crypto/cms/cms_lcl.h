/*
 * Copyright 2008-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_CMS_LCL_H
# define HEADER_CMS_LCL_H

# include <openssl/x509.h>

/*
 * Cryptographic message syntax (CMS) structures: taken from RFC3852
 */

/* Forward references */

typedef struct CMS_IssuerAndSerialNumber_st CMS_IssuerAndSerialNumber;
typedef struct CMS_EncapsulatedContentInfo_st CMS_EncapsulatedContentInfo;
typedef struct CMS_SignerIdentifier_st CMS_SignerIdentifier;
typedef struct CMS_SignedData_st CMS_SignedData;
typedef struct CMS_OtherRevocationInfoFormat_st CMS_OtherRevocationInfoFormat;
typedef struct CMS_OriginatorInfo_st CMS_OriginatorInfo;
typedef struct CMS_EncryptedContentInfo_st CMS_EncryptedContentInfo;
typedef struct CMS_EnvelopedData_st CMS_EnvelopedData;
typedef struct CMS_DigestedData_st CMS_DigestedData;
typedef struct CMS_EncryptedData_st CMS_EncryptedData;
typedef struct CMS_AuthenticatedData_st CMS_AuthenticatedData;
typedef struct CMS_CompressedData_st CMS_CompressedData;
typedef struct CMS_OtherCertificateFormat_st CMS_OtherCertificateFormat;
typedef struct CMS_KeyTransRecipientInfo_st CMS_KeyTransRecipientInfo;
typedef struct CMS_OriginatorPublicKey_st CMS_OriginatorPublicKey;
typedef struct CMS_OriginatorIdentifierOrKey_st CMS_OriginatorIdentifierOrKey;
typedef struct CMS_KeyAgreeRecipientInfo_st CMS_KeyAgreeRecipientInfo;
typedef struct CMS_RecipientKeyIdentifier_st CMS_RecipientKeyIdentifier;
typedef struct CMS_KeyAgreeRecipientIdentifier_st
    CMS_KeyAgreeRecipientIdentifier;
typedef struct CMS_KEKIdentifier_st CMS_KEKIdentifier;
typedef struct CMS_KEKRecipientInfo_st CMS_KEKRecipientInfo;
typedef struct CMS_PasswordRecipientInfo_st CMS_PasswordRecipientInfo;
typedef struct CMS_OtherRecipientInfo_st CMS_OtherRecipientInfo;
typedef struct CMS_ReceiptsFrom_st CMS_ReceiptsFrom;

struct CMS_ContentInfo_st {
    ASN1_OBJECT *contentType;
    union {
        ASN1_OCTET_STRING *data;
        CMS_SignedData *signedData;
        CMS_EnvelopedData *envelopedData;
        CMS_DigestedData *digestedData;
        CMS_EncryptedData *encryptedData;
        CMS_AuthenticatedData *authenticatedData;
        CMS_CompressedData *compressedData;
        ASN1_TYPE *other;
        /* Other types ... */
        void *otherData;
    } d;
};

DEFINE_STACK_OF(CMS_CertificateChoices)

struct CMS_SignedData_st {
    int32_t version;
    STACK_OF(X509_ALGOR) *digestAlgorithms;
    CMS_EncapsulatedContentInfo *encapContentInfo;
    STACK_OF(CMS_CertificateChoices) *certificates;
    STACK_OF(CMS_RevocationInfoChoice) *crls;
    STACK_OF(CMS_SignerInfo) *signerInfos;
};

struct CMS_EncapsulatedContentInfo_st {
    ASN1_OBJECT *eContentType;
    ASN1_OCTET_STRING *eContent;
    /* Set to 1 if incomplete structure only part set up */
    int partial;
};

struct CMS_SignerInfo_st {
    int32_t version;
    CMS_SignerIdentifier *sid;
    X509_ALGOR *digestAlgorithm;
    STACK_OF(X509_ATTRIBUTE) *signedAttrs;
    X509_ALGOR *signatureAlgorithm;
    ASN1_OCTET_STRING *signature;
    STACK_OF(X509_ATTRIBUTE) *unsignedAttrs;
    /* Signing certificate and key */
    X509 *signer;
    EVP_PKEY *pkey;
    /* Digest and public key context for alternative parameters */
    EVP_MD_CTX *mctx;
    EVP_PKEY_CTX *pctx;
};

struct CMS_SignerIdentifier_st {
    int type;
    union {
        CMS_IssuerAndSerialNumber *issuerAndSerialNumber;
        ASN1_OCTET_STRING *subjectKeyIdentifier;
    } d;
};

struct CMS_EnvelopedData_st {
    int32_t version;
    CMS_OriginatorInfo *originatorInfo;
    STACK_OF(CMS_RecipientInfo) *recipientInfos;
    CMS_EncryptedContentInfo *encryptedContentInfo;
    STACK_OF(X509_ATTRIBUTE) *unprotectedAttrs;
};

struct CMS_OriginatorInfo_st {
    STACK_OF(CMS_CertificateChoices) *certificates;
    STACK_OF(CMS_RevocationInfoChoice) *crls;
};

struct CMS_EncryptedContentInfo_st {
    ASN1_OBJECT *contentType;
    X509_ALGOR *contentEncryptionAlgorithm;
    ASN1_OCTET_STRING *encryptedContent;
    /* Content encryption algorithm and key */
    const EVP_CIPHER *cipher;
    unsigned char *key;
    size_t keylen;
    /* Set to 1 if we are debugging decrypt and don't fake keys for MMA */
    int debug;
};

struct CMS_RecipientInfo_st {
    int type;
    union {
        CMS_KeyTransRecipientInfo *ktri;
        CMS_KeyAgreeRecipientInfo *kari;
        CMS_KEKRecipientInfo *kekri;
        CMS_PasswordRecipientInfo *pwri;
        CMS_OtherRecipientInfo *ori;
    } d;
};

typedef CMS_SignerIdentifier CMS_RecipientIdentifier;

struct CMS_KeyTransRecipientInfo_st {
    int32_t version;
    CMS_RecipientIdentifier *rid;
    X509_ALGOR *keyEncryptionAlgorithm;
    ASN1_OCTET_STRING *encryptedKey;
    /* Recipient Key and cert */
    X509 *recip;
    EVP_PKEY *pkey;
    /* Public key context for this operation */
    EVP_PKEY_CTX *pctx;
};

struct CMS_KeyAgreeRecipientInfo_st {
    int32_t version;
    CMS_OriginatorIdentifierOrKey *originator;
    ASN1_OCTET_STRING *ukm;
    X509_ALGOR *keyEncryptionAlgorithm;
    STACK_OF(CMS_RecipientEncryptedKey) *recipientEncryptedKeys;
    /* Public key context associated with current operation */
    EVP_PKEY_CTX *pctx;
    /* Cipher context for CEK wrapping */
    EVP_CIPHER_CTX *ctx;
};

struct CMS_OriginatorIdentifierOrKey_st {
    int type;
    union {
        CMS_IssuerAndSerialNumber *issuerAndSerialNumber;
        ASN1_OCTET_STRING *subjectKeyIdentifier;
        CMS_OriginatorPublicKey *originatorKey;
    } d;
};

struct CMS_OriginatorPublicKey_st {
    X509_ALGOR *algorithm;
    ASN1_BIT_STRING *publicKey;
};

struct CMS_RecipientEncryptedKey_st {
    CMS_KeyAgreeRecipientIdentifier *rid;
    ASN1_OCTET_STRING *encryptedKey;
    /* Public key associated with this recipient */
    EVP_PKEY *pkey;
};

struct CMS_KeyAgreeRecipientIdentifier_st {
    int type;
    union {
        CMS_IssuerAndSerialNumber *issuerAndSerialNumber;
        CMS_RecipientKeyIdentifier *rKeyId;
    } d;
};

struct CMS_RecipientKeyIdentifier_st {
    ASN1_OCTET_STRING *subjectKeyIdentifier;
    ASN1_GENERALIZEDTIME *date;
    CMS_OtherKeyAttribute *other;
};

struct CMS_KEKRecipientInfo_st {
    int32_t version;
    CMS_KEKIdentifier *kekid;
    X509_ALGOR *keyEncryptionAlgorithm;
    ASN1_OCTET_STRING *encryptedKey;
    /* Extra info: symmetric key to use */
    unsigned char *key;
    size_t keylen;
};

struct CMS_KEKIdentifier_st {
    ASN1_OCTET_STRING *keyIdentifier;
    ASN1_GENERALIZEDTIME *date;
    CMS_OtherKeyAttribute *other;
};

struct CMS_PasswordRecipientInfo_st {
    int32_t version;
    X509_ALGOR *keyDerivationAlgorithm;
    X509_ALGOR *keyEncryptionAlgorithm;
    ASN1_OCTET_STRING *encryptedKey;
    /* Extra info: password to use */
    unsigned char *pass;
    size_t passlen;
};

struct CMS_OtherRecipientInfo_st {
    ASN1_OBJECT *oriType;
    ASN1_TYPE *oriValue;
};

struct CMS_DigestedData_st {
    int32_t version;
    X509_ALGOR *digestAlgorithm;
    CMS_EncapsulatedContentInfo *encapContentInfo;
    ASN1_OCTET_STRING *digest;
};

struct CMS_EncryptedData_st {
    int32_t version;
    CMS_EncryptedContentInfo *encryptedContentInfo;
    STACK_OF(X509_ATTRIBUTE) *unprotectedAttrs;
};

struct CMS_AuthenticatedData_st {
    int32_t version;
    CMS_OriginatorInfo *originatorInfo;
    STACK_OF(CMS_RecipientInfo) *recipientInfos;
    X509_ALGOR *macAlgorithm;
    X509_ALGOR *digestAlgorithm;
    CMS_EncapsulatedContentInfo *encapContentInfo;
    STACK_OF(X509_ATTRIBUTE) *authAttrs;
    ASN1_OCTET_STRING *mac;
    STACK_OF(X509_ATTRIBUTE) *unauthAttrs;
};

struct CMS_CompressedData_st {
    int32_t version;
    X509_ALGOR *compressionAlgorithm;
    STACK_OF(CMS_RecipientInfo) *recipientInfos;
    CMS_EncapsulatedContentInfo *encapContentInfo;
};

struct CMS_RevocationInfoChoice_st {
    int type;
    union {
        X509_CRL *crl;
        CMS_OtherRevocationInfoFormat *other;
    } d;
};

# define CMS_REVCHOICE_CRL               0
# define CMS_REVCHOICE_OTHER             1

struct CMS_OtherRevocationInfoFormat_st {
    ASN1_OBJECT *otherRevInfoFormat;
    ASN1_TYPE *otherRevInfo;
};

struct CMS_CertificateChoices {
    int type;
    union {
        X509 *certificate;
        ASN1_STRING *extendedCertificate; /* Obsolete */
        ASN1_STRING *v1AttrCert; /* Left encoded for now */
        ASN1_STRING *v2AttrCert; /* Left encoded for now */
        CMS_OtherCertificateFormat *other;
    } d;
};

# define CMS_CERTCHOICE_CERT             0
# define CMS_CERTCHOICE_EXCERT           1
# define CMS_CERTCHOICE_V1ACERT          2
# define CMS_CERTCHOICE_V2ACERT          3
# define CMS_CERTCHOICE_OTHER            4

struct CMS_OtherCertificateFormat_st {
    ASN1_OBJECT *otherCertFormat;
    ASN1_TYPE *otherCert;
};

/*
 * This is also defined in pkcs7.h but we duplicate it to allow the CMS code
 * to be independent of PKCS#7
 */

struct CMS_IssuerAndSerialNumber_st {
    X509_NAME *issuer;
    ASN1_INTEGER *serialNumber;
};

struct CMS_OtherKeyAttribute_st {
    ASN1_OBJECT *keyAttrId;
    ASN1_TYPE *keyAttr;
};

/* ESS structures */

# ifdef HEADER_X509V3_H

struct CMS_ReceiptRequest_st {
    ASN1_OCTET_STRING *signedContentIdentifier;
    CMS_ReceiptsFrom *receiptsFrom;
    STACK_OF(GENERAL_NAMES) *receiptsTo;
};

struct CMS_ReceiptsFrom_st {
    int type;
    union {
        int32_t allOrFirstTier;
        STACK_OF(GENERAL_NAMES) *receiptList;
    } d;
};
# endif

struct CMS_Receipt_st {
    int32_t version;
    ASN1_OBJECT *contentType;
    ASN1_OCTET_STRING *signedContentIdentifier;
    ASN1_OCTET_STRING *originatorSignatureValue;
};

DECLARE_ASN1_FUNCTIONS(CMS_ContentInfo)
DECLARE_ASN1_ITEM(CMS_SignerInfo)
DECLARE_ASN1_ITEM(CMS_IssuerAndSerialNumber)
DECLARE_ASN1_ITEM(CMS_Attributes_Sign)
DECLARE_ASN1_ITEM(CMS_Attributes_Verify)
DECLARE_ASN1_ITEM(CMS_RecipientInfo)
DECLARE_ASN1_ITEM(CMS_PasswordRecipientInfo)
DECLARE_ASN1_ALLOC_FUNCTIONS(CMS_IssuerAndSerialNumber)

# define CMS_SIGNERINFO_ISSUER_SERIAL    0
# define CMS_SIGNERINFO_KEYIDENTIFIER    1

# define CMS_RECIPINFO_ISSUER_SERIAL     0
# define CMS_RECIPINFO_KEYIDENTIFIER     1

# define CMS_REK_ISSUER_SERIAL           0
# define CMS_REK_KEYIDENTIFIER           1

# define CMS_OIK_ISSUER_SERIAL           0
# define CMS_OIK_KEYIDENTIFIER           1
# define CMS_OIK_PUBKEY                  2

BIO *cms_content_bio(CMS_ContentInfo *cms);

CMS_ContentInfo *cms_Data_create(void);

CMS_ContentInfo *cms_DigestedData_create(const EVP_MD *md);
BIO *cms_DigestedData_init_bio(CMS_ContentInfo *cms);
int cms_DigestedData_do_final(CMS_ContentInfo *cms, BIO *chain, int verify);

BIO *cms_SignedData_init_bio(CMS_ContentInfo *cms);
int cms_SignedData_final(CMS_ContentInfo *cms, BIO *chain);
int cms_set1_SignerIdentifier(CMS_SignerIdentifier *sid, X509 *cert,
                              int type);
int cms_SignerIdentifier_get0_signer_id(CMS_SignerIdentifier *sid,
                                        ASN1_OCTET_STRING **keyid,
                                        X509_NAME **issuer,
                                        ASN1_INTEGER **sno);
int cms_SignerIdentifier_cert_cmp(CMS_SignerIdentifier *sid, X509 *cert);

CMS_ContentInfo *cms_CompressedData_create(int comp_nid);
BIO *cms_CompressedData_init_bio(CMS_ContentInfo *cms);

BIO *cms_DigestAlgorithm_init_bio(X509_ALGOR *digestAlgorithm);
int cms_DigestAlgorithm_find_ctx(EVP_MD_CTX *mctx, BIO *chain,
                                 X509_ALGOR *mdalg);

int cms_ias_cert_cmp(CMS_IssuerAndSerialNumber *ias, X509 *cert);
int cms_keyid_cert_cmp(ASN1_OCTET_STRING *keyid, X509 *cert);
int cms_set1_ias(CMS_IssuerAndSerialNumber **pias, X509 *cert);
int cms_set1_keyid(ASN1_OCTET_STRING **pkeyid, X509 *cert);

BIO *cms_EncryptedContent_init_bio(CMS_EncryptedContentInfo *ec);
BIO *cms_EncryptedData_init_bio(CMS_ContentInfo *cms);
int cms_EncryptedContent_init(CMS_EncryptedContentInfo *ec,
                              const EVP_CIPHER *cipher,
                              const unsigned char *key, size_t keylen);

int cms_Receipt_verify(CMS_ContentInfo *cms, CMS_ContentInfo *req_cms);
int cms_msgSigDigest_add1(CMS_SignerInfo *dest, CMS_SignerInfo *src);
ASN1_OCTET_STRING *cms_encode_Receipt(CMS_SignerInfo *si);

BIO *cms_EnvelopedData_init_bio(CMS_ContentInfo *cms);
CMS_EnvelopedData *cms_get0_enveloped(CMS_ContentInfo *cms);
int cms_env_asn1_ctrl(CMS_RecipientInfo *ri, int cmd);
int cms_pkey_get_ri_type(EVP_PKEY *pk);
/* KARI routines */
int cms_RecipientInfo_kari_init(CMS_RecipientInfo *ri, X509 *recip,
                                EVP_PKEY *pk, unsigned int flags);
int cms_RecipientInfo_kari_encrypt(CMS_ContentInfo *cms,
                                   CMS_RecipientInfo *ri);

/* PWRI routines */
int cms_RecipientInfo_pwri_crypt(CMS_ContentInfo *cms, CMS_RecipientInfo *ri,
                                 int en_de);

DECLARE_ASN1_ITEM(CMS_CertificateChoices)
DECLARE_ASN1_ITEM(CMS_DigestedData)
DECLARE_ASN1_ITEM(CMS_EncryptedData)
DECLARE_ASN1_ITEM(CMS_EnvelopedData)
DECLARE_ASN1_ITEM(CMS_KEKRecipientInfo)
DECLARE_ASN1_ITEM(CMS_KeyAgreeRecipientInfo)
DECLARE_ASN1_ITEM(CMS_KeyTransRecipientInfo)
DECLARE_ASN1_ITEM(CMS_OriginatorPublicKey)
DECLARE_ASN1_ITEM(CMS_OtherKeyAttribute)
DECLARE_ASN1_ITEM(CMS_Receipt)
DECLARE_ASN1_ITEM(CMS_ReceiptRequest)
DECLARE_ASN1_ITEM(CMS_RecipientEncryptedKey)
DECLARE_ASN1_ITEM(CMS_RecipientKeyIdentifier)
DECLARE_ASN1_ITEM(CMS_RevocationInfoChoice)
DECLARE_ASN1_ITEM(CMS_SignedData)
DECLARE_ASN1_ITEM(CMS_CompressedData)

#endif
