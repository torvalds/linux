/*
 * Copyright 2014-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/refcount.h"

/*
 * This structure holds all parameters associated with a verify operation by
 * including an X509_VERIFY_PARAM structure in related structures the
 * parameters used can be customized
 */

struct X509_VERIFY_PARAM_st {
    char *name;
    time_t check_time;          /* Time to use */
    uint32_t inh_flags;         /* Inheritance flags */
    unsigned long flags;        /* Various verify flags */
    int purpose;                /* purpose to check untrusted certificates */
    int trust;                  /* trust setting to check */
    int depth;                  /* Verify depth */
    int auth_level;             /* Security level for chain verification */
    STACK_OF(ASN1_OBJECT) *policies; /* Permissible policies */
    /* Peer identity details */
    STACK_OF(OPENSSL_STRING) *hosts; /* Set of acceptable names */
    unsigned int hostflags;     /* Flags to control matching features */
    char *peername;             /* Matching hostname in peer certificate */
    char *email;                /* If not NULL email address to match */
    size_t emaillen;
    unsigned char *ip;          /* If not NULL IP address to match */
    size_t iplen;               /* Length of IP address */
};

/* No error callback if depth < 0 */
int x509_check_cert_time(X509_STORE_CTX *ctx, X509 *x, int depth);

/* a sequence of these are used */
struct x509_attributes_st {
    ASN1_OBJECT *object;
    STACK_OF(ASN1_TYPE) *set;
};

struct X509_extension_st {
    ASN1_OBJECT *object;
    ASN1_BOOLEAN critical;
    ASN1_OCTET_STRING value;
};

/*
 * Method to handle CRL access. In general a CRL could be very large (several
 * Mb) and can consume large amounts of resources if stored in memory by
 * multiple processes. This method allows general CRL operations to be
 * redirected to more efficient callbacks: for example a CRL entry database.
 */

#define X509_CRL_METHOD_DYNAMIC         1

struct x509_crl_method_st {
    int flags;
    int (*crl_init) (X509_CRL *crl);
    int (*crl_free) (X509_CRL *crl);
    int (*crl_lookup) (X509_CRL *crl, X509_REVOKED **ret,
                       ASN1_INTEGER *ser, X509_NAME *issuer);
    int (*crl_verify) (X509_CRL *crl, EVP_PKEY *pk);
};

struct x509_lookup_method_st {
    char *name;
    int (*new_item) (X509_LOOKUP *ctx);
    void (*free) (X509_LOOKUP *ctx);
    int (*init) (X509_LOOKUP *ctx);
    int (*shutdown) (X509_LOOKUP *ctx);
    int (*ctrl) (X509_LOOKUP *ctx, int cmd, const char *argc, long argl,
                 char **ret);
    int (*get_by_subject) (X509_LOOKUP *ctx, X509_LOOKUP_TYPE type,
                           X509_NAME *name, X509_OBJECT *ret);
    int (*get_by_issuer_serial) (X509_LOOKUP *ctx, X509_LOOKUP_TYPE type,
                                 X509_NAME *name, ASN1_INTEGER *serial,
                                 X509_OBJECT *ret);
    int (*get_by_fingerprint) (X509_LOOKUP *ctx, X509_LOOKUP_TYPE type,
                               const unsigned char *bytes, int len,
                               X509_OBJECT *ret);
    int (*get_by_alias) (X509_LOOKUP *ctx, X509_LOOKUP_TYPE type,
                         const char *str, int len, X509_OBJECT *ret);
};

/* This is the functions plus an instance of the local variables. */
struct x509_lookup_st {
    int init;                   /* have we been started */
    int skip;                   /* don't use us. */
    X509_LOOKUP_METHOD *method; /* the functions */
    void *method_data;          /* method data */
    X509_STORE *store_ctx;      /* who owns us */
};

/*
 * This is used to hold everything.  It is used for all certificate
 * validation.  Once we have a certificate chain, the 'verify' function is
 * then called to actually check the cert chain.
 */
struct x509_store_st {
    /* The following is a cache of trusted certs */
    int cache;                  /* if true, stash any hits */
    STACK_OF(X509_OBJECT) *objs; /* Cache of all objects */
    /* These are external lookup methods */
    STACK_OF(X509_LOOKUP) *get_cert_methods;
    X509_VERIFY_PARAM *param;
    /* Callbacks for various operations */
    /* called to verify a certificate */
    int (*verify) (X509_STORE_CTX *ctx);
    /* error callback */
    int (*verify_cb) (int ok, X509_STORE_CTX *ctx);
    /* get issuers cert from ctx */
    int (*get_issuer) (X509 **issuer, X509_STORE_CTX *ctx, X509 *x);
    /* check issued */
    int (*check_issued) (X509_STORE_CTX *ctx, X509 *x, X509 *issuer);
    /* Check revocation status of chain */
    int (*check_revocation) (X509_STORE_CTX *ctx);
    /* retrieve CRL */
    int (*get_crl) (X509_STORE_CTX *ctx, X509_CRL **crl, X509 *x);
    /* Check CRL validity */
    int (*check_crl) (X509_STORE_CTX *ctx, X509_CRL *crl);
    /* Check certificate against CRL */
    int (*cert_crl) (X509_STORE_CTX *ctx, X509_CRL *crl, X509 *x);
    /* Check policy status of the chain */
    int (*check_policy) (X509_STORE_CTX *ctx);
    STACK_OF(X509) *(*lookup_certs) (X509_STORE_CTX *ctx, X509_NAME *nm);
    STACK_OF(X509_CRL) *(*lookup_crls) (X509_STORE_CTX *ctx, X509_NAME *nm);
    int (*cleanup) (X509_STORE_CTX *ctx);
    CRYPTO_EX_DATA ex_data;
    CRYPTO_REF_COUNT references;
    CRYPTO_RWLOCK *lock;
};

typedef struct lookup_dir_hashes_st BY_DIR_HASH;
typedef struct lookup_dir_entry_st BY_DIR_ENTRY;
DEFINE_STACK_OF(BY_DIR_HASH)
DEFINE_STACK_OF(BY_DIR_ENTRY)
typedef STACK_OF(X509_NAME_ENTRY) STACK_OF_X509_NAME_ENTRY;
DEFINE_STACK_OF(STACK_OF_X509_NAME_ENTRY)

void x509_set_signature_info(X509_SIG_INFO *siginf, const X509_ALGOR *alg,
                             const ASN1_STRING *sig);
