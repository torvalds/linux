/*
 * Copyright 2015-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*-
 * MessageImprint ::= SEQUENCE  {
 *      hashAlgorithm                AlgorithmIdentifier,
 *      hashedMessage                OCTET STRING  }
 */
struct TS_msg_imprint_st {
    X509_ALGOR *hash_algo;
    ASN1_OCTET_STRING *hashed_msg;
};

/*-
 * TimeStampResp ::= SEQUENCE  {
 *     status                  PKIStatusInfo,
 *     timeStampToken          TimeStampToken     OPTIONAL }
 */
struct TS_resp_st {
    TS_STATUS_INFO *status_info;
    PKCS7 *token;
    TS_TST_INFO *tst_info;
};

/*-
 * TimeStampReq ::= SEQUENCE  {
 *    version                  INTEGER  { v1(1) },
 *    messageImprint           MessageImprint,
 *      --a hash algorithm OID and the hash value of the data to be
 *      --time-stamped
 *    reqPolicy                TSAPolicyId                OPTIONAL,
 *    nonce                    INTEGER                    OPTIONAL,
 *    certReq                  BOOLEAN                    DEFAULT FALSE,
 *    extensions               [0] IMPLICIT Extensions    OPTIONAL  }
 */
struct TS_req_st {
    ASN1_INTEGER *version;
    TS_MSG_IMPRINT *msg_imprint;
    ASN1_OBJECT *policy_id;
    ASN1_INTEGER *nonce;
    ASN1_BOOLEAN cert_req;
    STACK_OF(X509_EXTENSION) *extensions;
};

/*-
 * Accuracy ::= SEQUENCE {
 *                 seconds        INTEGER           OPTIONAL,
 *                 millis     [0] INTEGER  (1..999) OPTIONAL,
 *                 micros     [1] INTEGER  (1..999) OPTIONAL  }
 */
struct TS_accuracy_st {
    ASN1_INTEGER *seconds;
    ASN1_INTEGER *millis;
    ASN1_INTEGER *micros;
};

/*-
 * TSTInfo ::= SEQUENCE  {
 *     version                      INTEGER  { v1(1) },
 *     policy                       TSAPolicyId,
 *     messageImprint               MessageImprint,
 *       -- MUST have the same value as the similar field in
 *       -- TimeStampReq
 *     serialNumber                 INTEGER,
 *      -- Time-Stamping users MUST be ready to accommodate integers
 *      -- up to 160 bits.
 *     genTime                      GeneralizedTime,
 *     accuracy                     Accuracy                 OPTIONAL,
 *     ordering                     BOOLEAN             DEFAULT FALSE,
 *     nonce                        INTEGER                  OPTIONAL,
 *       -- MUST be present if the similar field was present
 *       -- in TimeStampReq.  In that case it MUST have the same value.
 *     tsa                          [0] GeneralName          OPTIONAL,
 *     extensions                   [1] IMPLICIT Extensions  OPTIONAL   }
 */
struct TS_tst_info_st {
    ASN1_INTEGER *version;
    ASN1_OBJECT *policy_id;
    TS_MSG_IMPRINT *msg_imprint;
    ASN1_INTEGER *serial;
    ASN1_GENERALIZEDTIME *time;
    TS_ACCURACY *accuracy;
    ASN1_BOOLEAN ordering;
    ASN1_INTEGER *nonce;
    GENERAL_NAME *tsa;
    STACK_OF(X509_EXTENSION) *extensions;
};

struct TS_status_info_st {
    ASN1_INTEGER *status;
    STACK_OF(ASN1_UTF8STRING) *text;
    ASN1_BIT_STRING *failure_info;
};

/*-
 * IssuerSerial ::= SEQUENCE {
 *         issuer                   GeneralNames,
 *         serialNumber             CertificateSerialNumber
 *         }
 */
struct ESS_issuer_serial {
    STACK_OF(GENERAL_NAME) *issuer;
    ASN1_INTEGER *serial;
};

/*-
 * ESSCertID ::=  SEQUENCE {
 *         certHash                 Hash,
 *         issuerSerial             IssuerSerial OPTIONAL
 * }
 */
struct ESS_cert_id {
    ASN1_OCTET_STRING *hash;    /* Always SHA-1 digest. */
    ESS_ISSUER_SERIAL *issuer_serial;
};

/*-
 * SigningCertificate ::=  SEQUENCE {
 *        certs        SEQUENCE OF ESSCertID,
 *        policies     SEQUENCE OF PolicyInformation OPTIONAL
 * }
 */
struct ESS_signing_cert {
    STACK_OF(ESS_CERT_ID) *cert_ids;
    STACK_OF(POLICYINFO) *policy_info;
};

/*-
 * ESSCertIDv2 ::=  SEQUENCE {
 *        hashAlgorithm           AlgorithmIdentifier
 *                DEFAULT {algorithm id-sha256},
 *        certHash                Hash,
 *        issuerSerial            IssuerSerial OPTIONAL
 * }
 */

struct ESS_cert_id_v2_st {
    X509_ALGOR *hash_alg;       /* Default: SHA-256 */
    ASN1_OCTET_STRING *hash;
    ESS_ISSUER_SERIAL *issuer_serial;
};

/*-
 * SigningCertificateV2 ::= SEQUENCE {
 *        certs                   SEQUENCE OF ESSCertIDv2,
 *        policies                SEQUENCE OF PolicyInformation OPTIONAL
 * }
 */

struct ESS_signing_cert_v2_st {
    STACK_OF(ESS_CERT_ID_V2) *cert_ids;
    STACK_OF(POLICYINFO) *policy_info;
};


struct TS_resp_ctx {
    X509 *signer_cert;
    EVP_PKEY *signer_key;
    const EVP_MD *signer_md;
    const EVP_MD *ess_cert_id_digest;
    STACK_OF(X509) *certs;      /* Certs to include in signed data. */
    STACK_OF(ASN1_OBJECT) *policies; /* Acceptable policies. */
    ASN1_OBJECT *default_policy; /* It may appear in policies, too. */
    STACK_OF(EVP_MD) *mds;      /* Acceptable message digests. */
    ASN1_INTEGER *seconds;      /* accuracy, 0 means not specified. */
    ASN1_INTEGER *millis;       /* accuracy, 0 means not specified. */
    ASN1_INTEGER *micros;       /* accuracy, 0 means not specified. */
    unsigned clock_precision_digits; /* fraction of seconds in time stamp
                                      * token. */
    unsigned flags;             /* Optional info, see values above. */
    /* Callback functions. */
    TS_serial_cb serial_cb;
    void *serial_cb_data;       /* User data for serial_cb. */
    TS_time_cb time_cb;
    void *time_cb_data;         /* User data for time_cb. */
    TS_extension_cb extension_cb;
    void *extension_cb_data;    /* User data for extension_cb. */
    /* These members are used only while creating the response. */
    TS_REQ *request;
    TS_RESP *response;
    TS_TST_INFO *tst_info;
};

struct TS_verify_ctx {
    /* Set this to the union of TS_VFY_... flags you want to carry out. */
    unsigned flags;
    /* Must be set only with TS_VFY_SIGNATURE. certs is optional. */
    X509_STORE *store;
    STACK_OF(X509) *certs;
    /* Must be set only with TS_VFY_POLICY. */
    ASN1_OBJECT *policy;
    /*
     * Must be set only with TS_VFY_IMPRINT. If md_alg is NULL, the
     * algorithm from the response is used.
     */
    X509_ALGOR *md_alg;
    unsigned char *imprint;
    unsigned imprint_len;
    /* Must be set only with TS_VFY_DATA. */
    BIO *data;
    /* Must be set only with TS_VFY_TSA_NAME. */
    ASN1_INTEGER *nonce;
    /* Must be set only with TS_VFY_TSA_NAME. */
    GENERAL_NAME *tsa_name;
};
