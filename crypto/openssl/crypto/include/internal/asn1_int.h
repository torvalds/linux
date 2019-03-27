/*
 * Copyright 2015-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* Internal ASN1 structures and functions: not for application use */

/* ASN1 public key method structure */

struct evp_pkey_asn1_method_st {
    int pkey_id;
    int pkey_base_id;
    unsigned long pkey_flags;
    char *pem_str;
    char *info;
    int (*pub_decode) (EVP_PKEY *pk, X509_PUBKEY *pub);
    int (*pub_encode) (X509_PUBKEY *pub, const EVP_PKEY *pk);
    int (*pub_cmp) (const EVP_PKEY *a, const EVP_PKEY *b);
    int (*pub_print) (BIO *out, const EVP_PKEY *pkey, int indent,
                      ASN1_PCTX *pctx);
    int (*priv_decode) (EVP_PKEY *pk, const PKCS8_PRIV_KEY_INFO *p8inf);
    int (*priv_encode) (PKCS8_PRIV_KEY_INFO *p8, const EVP_PKEY *pk);
    int (*priv_print) (BIO *out, const EVP_PKEY *pkey, int indent,
                       ASN1_PCTX *pctx);
    int (*pkey_size) (const EVP_PKEY *pk);
    int (*pkey_bits) (const EVP_PKEY *pk);
    int (*pkey_security_bits) (const EVP_PKEY *pk);
    int (*param_decode) (EVP_PKEY *pkey,
                         const unsigned char **pder, int derlen);
    int (*param_encode) (const EVP_PKEY *pkey, unsigned char **pder);
    int (*param_missing) (const EVP_PKEY *pk);
    int (*param_copy) (EVP_PKEY *to, const EVP_PKEY *from);
    int (*param_cmp) (const EVP_PKEY *a, const EVP_PKEY *b);
    int (*param_print) (BIO *out, const EVP_PKEY *pkey, int indent,
                        ASN1_PCTX *pctx);
    int (*sig_print) (BIO *out,
                      const X509_ALGOR *sigalg, const ASN1_STRING *sig,
                      int indent, ASN1_PCTX *pctx);
    void (*pkey_free) (EVP_PKEY *pkey);
    int (*pkey_ctrl) (EVP_PKEY *pkey, int op, long arg1, void *arg2);
    /* Legacy functions for old PEM */
    int (*old_priv_decode) (EVP_PKEY *pkey,
                            const unsigned char **pder, int derlen);
    int (*old_priv_encode) (const EVP_PKEY *pkey, unsigned char **pder);
    /* Custom ASN1 signature verification */
    int (*item_verify) (EVP_MD_CTX *ctx, const ASN1_ITEM *it, void *asn,
                        X509_ALGOR *a, ASN1_BIT_STRING *sig, EVP_PKEY *pkey);
    int (*item_sign) (EVP_MD_CTX *ctx, const ASN1_ITEM *it, void *asn,
                      X509_ALGOR *alg1, X509_ALGOR *alg2,
                      ASN1_BIT_STRING *sig);
    int (*siginf_set) (X509_SIG_INFO *siginf, const X509_ALGOR *alg,
                       const ASN1_STRING *sig);
    /* Check */
    int (*pkey_check) (const EVP_PKEY *pk);
    int (*pkey_public_check) (const EVP_PKEY *pk);
    int (*pkey_param_check) (const EVP_PKEY *pk);
    /* Get/set raw private/public key data */
    int (*set_priv_key) (EVP_PKEY *pk, const unsigned char *priv, size_t len);
    int (*set_pub_key) (EVP_PKEY *pk, const unsigned char *pub, size_t len);
    int (*get_priv_key) (const EVP_PKEY *pk, unsigned char *priv, size_t *len);
    int (*get_pub_key) (const EVP_PKEY *pk, unsigned char *pub, size_t *len);
} /* EVP_PKEY_ASN1_METHOD */ ;

DEFINE_STACK_OF_CONST(EVP_PKEY_ASN1_METHOD)

extern const EVP_PKEY_ASN1_METHOD cmac_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD dh_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD dhx_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD dsa_asn1_meths[5];
extern const EVP_PKEY_ASN1_METHOD eckey_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD ecx25519_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD ecx448_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD ed25519_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD ed448_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD sm2_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD poly1305_asn1_meth;

extern const EVP_PKEY_ASN1_METHOD hmac_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD rsa_asn1_meths[2];
extern const EVP_PKEY_ASN1_METHOD rsa_pss_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD siphash_asn1_meth;

/*
 * These are used internally in the ASN1_OBJECT to keep track of whether the
 * names and data need to be free()ed
 */
# define ASN1_OBJECT_FLAG_DYNAMIC         0x01/* internal use */
# define ASN1_OBJECT_FLAG_CRITICAL        0x02/* critical x509v3 object id */
# define ASN1_OBJECT_FLAG_DYNAMIC_STRINGS 0x04/* internal use */
# define ASN1_OBJECT_FLAG_DYNAMIC_DATA    0x08/* internal use */
struct asn1_object_st {
    const char *sn, *ln;
    int nid;
    int length;
    const unsigned char *data;  /* data remains const after init */
    int flags;                  /* Should we free this one */
};

/* ASN1 print context structure */

struct asn1_pctx_st {
    unsigned long flags;
    unsigned long nm_flags;
    unsigned long cert_flags;
    unsigned long oid_flags;
    unsigned long str_flags;
} /* ASN1_PCTX */ ;

int asn1_d2i_read_bio(BIO *in, BUF_MEM **pb);
