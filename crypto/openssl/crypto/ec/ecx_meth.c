/*
 * Copyright 2006-2019 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/x509.h>
#include <openssl/ec.h>
#include <openssl/rand.h>
#include "internal/asn1_int.h"
#include "internal/evp_int.h"
#include "ec_lcl.h"
#include "curve448/curve448_lcl.h"

#define X25519_BITS          253
#define X25519_SECURITY_BITS 128

#define ED25519_SIGSIZE      64

#define X448_BITS            448
#define ED448_BITS           456
#define X448_SECURITY_BITS   224

#define ED448_SIGSIZE        114

#define ISX448(id)      ((id) == EVP_PKEY_X448)
#define IS25519(id)     ((id) == EVP_PKEY_X25519 || (id) == EVP_PKEY_ED25519)
#define KEYLENID(id)    (IS25519(id) ? X25519_KEYLEN \
                                     : ((id) == EVP_PKEY_X448 ? X448_KEYLEN \
                                                              : ED448_KEYLEN))
#define KEYLEN(p)       KEYLENID((p)->ameth->pkey_id)


typedef enum {
    KEY_OP_PUBLIC,
    KEY_OP_PRIVATE,
    KEY_OP_KEYGEN
} ecx_key_op_t;

/* Setup EVP_PKEY using public, private or generation */
static int ecx_key_op(EVP_PKEY *pkey, int id, const X509_ALGOR *palg,
                      const unsigned char *p, int plen, ecx_key_op_t op)
{
    ECX_KEY *key = NULL;
    unsigned char *privkey, *pubkey;

    if (op != KEY_OP_KEYGEN) {
        if (palg != NULL) {
            int ptype;

            /* Algorithm parameters must be absent */
            X509_ALGOR_get0(NULL, &ptype, NULL, palg);
            if (ptype != V_ASN1_UNDEF) {
                ECerr(EC_F_ECX_KEY_OP, EC_R_INVALID_ENCODING);
                return 0;
            }
        }

        if (p == NULL || plen != KEYLENID(id)) {
            ECerr(EC_F_ECX_KEY_OP, EC_R_INVALID_ENCODING);
            return 0;
        }
    }

    key = OPENSSL_zalloc(sizeof(*key));
    if (key == NULL) {
        ECerr(EC_F_ECX_KEY_OP, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    pubkey = key->pubkey;

    if (op == KEY_OP_PUBLIC) {
        memcpy(pubkey, p, plen);
    } else {
        privkey = key->privkey = OPENSSL_secure_malloc(KEYLENID(id));
        if (privkey == NULL) {
            ECerr(EC_F_ECX_KEY_OP, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        if (op == KEY_OP_KEYGEN) {
            if (RAND_priv_bytes(privkey, KEYLENID(id)) <= 0) {
                OPENSSL_secure_free(privkey);
                key->privkey = NULL;
                goto err;
            }
            if (id == EVP_PKEY_X25519) {
                privkey[0] &= 248;
                privkey[X25519_KEYLEN - 1] &= 127;
                privkey[X25519_KEYLEN - 1] |= 64;
            } else if (id == EVP_PKEY_X448) {
                privkey[0] &= 252;
                privkey[X448_KEYLEN - 1] |= 128;
            }
        } else {
            memcpy(privkey, p, KEYLENID(id));
        }
        switch (id) {
        case EVP_PKEY_X25519:
            X25519_public_from_private(pubkey, privkey);
            break;
        case EVP_PKEY_ED25519:
            ED25519_public_from_private(pubkey, privkey);
            break;
        case EVP_PKEY_X448:
            X448_public_from_private(pubkey, privkey);
            break;
        case EVP_PKEY_ED448:
            ED448_public_from_private(pubkey, privkey);
            break;
        }
    }

    EVP_PKEY_assign(pkey, id, key);
    return 1;
 err:
    OPENSSL_free(key);
    return 0;
}

static int ecx_pub_encode(X509_PUBKEY *pk, const EVP_PKEY *pkey)
{
    const ECX_KEY *ecxkey = pkey->pkey.ecx;
    unsigned char *penc;

    if (ecxkey == NULL) {
        ECerr(EC_F_ECX_PUB_ENCODE, EC_R_INVALID_KEY);
        return 0;
    }

    penc = OPENSSL_memdup(ecxkey->pubkey, KEYLEN(pkey));
    if (penc == NULL) {
        ECerr(EC_F_ECX_PUB_ENCODE, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    if (!X509_PUBKEY_set0_param(pk, OBJ_nid2obj(pkey->ameth->pkey_id),
                                V_ASN1_UNDEF, NULL, penc, KEYLEN(pkey))) {
        OPENSSL_free(penc);
        ECerr(EC_F_ECX_PUB_ENCODE, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    return 1;
}

static int ecx_pub_decode(EVP_PKEY *pkey, X509_PUBKEY *pubkey)
{
    const unsigned char *p;
    int pklen;
    X509_ALGOR *palg;

    if (!X509_PUBKEY_get0_param(NULL, &p, &pklen, &palg, pubkey))
        return 0;
    return ecx_key_op(pkey, pkey->ameth->pkey_id, palg, p, pklen,
                      KEY_OP_PUBLIC);
}

static int ecx_pub_cmp(const EVP_PKEY *a, const EVP_PKEY *b)
{
    const ECX_KEY *akey = a->pkey.ecx;
    const ECX_KEY *bkey = b->pkey.ecx;

    if (akey == NULL || bkey == NULL)
        return -2;

    return CRYPTO_memcmp(akey->pubkey, bkey->pubkey, KEYLEN(a)) == 0;
}

static int ecx_priv_decode(EVP_PKEY *pkey, const PKCS8_PRIV_KEY_INFO *p8)
{
    const unsigned char *p;
    int plen;
    ASN1_OCTET_STRING *oct = NULL;
    const X509_ALGOR *palg;
    int rv;

    if (!PKCS8_pkey_get0(NULL, &p, &plen, &palg, p8))
        return 0;

    oct = d2i_ASN1_OCTET_STRING(NULL, &p, plen);
    if (oct == NULL) {
        p = NULL;
        plen = 0;
    } else {
        p = ASN1_STRING_get0_data(oct);
        plen = ASN1_STRING_length(oct);
    }

    rv = ecx_key_op(pkey, pkey->ameth->pkey_id, palg, p, plen, KEY_OP_PRIVATE);
    ASN1_OCTET_STRING_free(oct);
    return rv;
}

static int ecx_priv_encode(PKCS8_PRIV_KEY_INFO *p8, const EVP_PKEY *pkey)
{
    const ECX_KEY *ecxkey = pkey->pkey.ecx;
    ASN1_OCTET_STRING oct;
    unsigned char *penc = NULL;
    int penclen;

    if (ecxkey == NULL || ecxkey->privkey == NULL) {
        ECerr(EC_F_ECX_PRIV_ENCODE, EC_R_INVALID_PRIVATE_KEY);
        return 0;
    }

    oct.data = ecxkey->privkey;
    oct.length = KEYLEN(pkey);
    oct.flags = 0;

    penclen = i2d_ASN1_OCTET_STRING(&oct, &penc);
    if (penclen < 0) {
        ECerr(EC_F_ECX_PRIV_ENCODE, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    if (!PKCS8_pkey_set0(p8, OBJ_nid2obj(pkey->ameth->pkey_id), 0,
                         V_ASN1_UNDEF, NULL, penc, penclen)) {
        OPENSSL_clear_free(penc, penclen);
        ECerr(EC_F_ECX_PRIV_ENCODE, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    return 1;
}

static int ecx_size(const EVP_PKEY *pkey)
{
    return KEYLEN(pkey);
}

static int ecx_bits(const EVP_PKEY *pkey)
{
    if (IS25519(pkey->ameth->pkey_id)) {
        return X25519_BITS;
    } else if(ISX448(pkey->ameth->pkey_id)) {
        return X448_BITS;
    } else {
        return ED448_BITS;
    }
}

static int ecx_security_bits(const EVP_PKEY *pkey)
{
    if (IS25519(pkey->ameth->pkey_id)) {
        return X25519_SECURITY_BITS;
    } else {
        return X448_SECURITY_BITS;
    }
}

static void ecx_free(EVP_PKEY *pkey)
{
    if (pkey->pkey.ecx != NULL)
        OPENSSL_secure_clear_free(pkey->pkey.ecx->privkey, KEYLEN(pkey));
    OPENSSL_free(pkey->pkey.ecx);
}

/* "parameters" are always equal */
static int ecx_cmp_parameters(const EVP_PKEY *a, const EVP_PKEY *b)
{
    return 1;
}

static int ecx_key_print(BIO *bp, const EVP_PKEY *pkey, int indent,
                         ASN1_PCTX *ctx, ecx_key_op_t op)
{
    const ECX_KEY *ecxkey = pkey->pkey.ecx;
    const char *nm = OBJ_nid2ln(pkey->ameth->pkey_id);

    if (op == KEY_OP_PRIVATE) {
        if (ecxkey == NULL || ecxkey->privkey == NULL) {
            if (BIO_printf(bp, "%*s<INVALID PRIVATE KEY>\n", indent, "") <= 0)
                return 0;
            return 1;
        }
        if (BIO_printf(bp, "%*s%s Private-Key:\n", indent, "", nm) <= 0)
            return 0;
        if (BIO_printf(bp, "%*spriv:\n", indent, "") <= 0)
            return 0;
        if (ASN1_buf_print(bp, ecxkey->privkey, KEYLEN(pkey),
                           indent + 4) == 0)
            return 0;
    } else {
        if (ecxkey == NULL) {
            if (BIO_printf(bp, "%*s<INVALID PUBLIC KEY>\n", indent, "") <= 0)
                return 0;
            return 1;
        }
        if (BIO_printf(bp, "%*s%s Public-Key:\n", indent, "", nm) <= 0)
            return 0;
    }
    if (BIO_printf(bp, "%*spub:\n", indent, "") <= 0)
        return 0;

    if (ASN1_buf_print(bp, ecxkey->pubkey, KEYLEN(pkey),
                       indent + 4) == 0)
        return 0;
    return 1;
}

static int ecx_priv_print(BIO *bp, const EVP_PKEY *pkey, int indent,
                          ASN1_PCTX *ctx)
{
    return ecx_key_print(bp, pkey, indent, ctx, KEY_OP_PRIVATE);
}

static int ecx_pub_print(BIO *bp, const EVP_PKEY *pkey, int indent,
                         ASN1_PCTX *ctx)
{
    return ecx_key_print(bp, pkey, indent, ctx, KEY_OP_PUBLIC);
}

static int ecx_ctrl(EVP_PKEY *pkey, int op, long arg1, void *arg2)
{
    switch (op) {

    case ASN1_PKEY_CTRL_SET1_TLS_ENCPT:
        return ecx_key_op(pkey, pkey->ameth->pkey_id, NULL, arg2, arg1,
                          KEY_OP_PUBLIC);

    case ASN1_PKEY_CTRL_GET1_TLS_ENCPT:
        if (pkey->pkey.ecx != NULL) {
            unsigned char **ppt = arg2;

            *ppt = OPENSSL_memdup(pkey->pkey.ecx->pubkey, KEYLEN(pkey));
            if (*ppt != NULL)
                return KEYLEN(pkey);
        }
        return 0;

    default:
        return -2;

    }
}

static int ecd_ctrl(EVP_PKEY *pkey, int op, long arg1, void *arg2)
{
    switch (op) {
    case ASN1_PKEY_CTRL_DEFAULT_MD_NID:
        /* We currently only support Pure EdDSA which takes no digest */
        *(int *)arg2 = NID_undef;
        return 2;

    default:
        return -2;

    }
}

static int ecx_set_priv_key(EVP_PKEY *pkey, const unsigned char *priv,
                            size_t len)
{
    return ecx_key_op(pkey, pkey->ameth->pkey_id, NULL, priv, len,
                       KEY_OP_PRIVATE);
}

static int ecx_set_pub_key(EVP_PKEY *pkey, const unsigned char *pub, size_t len)
{
    return ecx_key_op(pkey, pkey->ameth->pkey_id, NULL, pub, len,
                      KEY_OP_PUBLIC);
}

static int ecx_get_priv_key(const EVP_PKEY *pkey, unsigned char *priv,
                            size_t *len)
{
    const ECX_KEY *key = pkey->pkey.ecx;

    if (priv == NULL) {
        *len = KEYLENID(pkey->ameth->pkey_id);
        return 1;
    }

    if (key == NULL
            || key->privkey == NULL
            || *len < (size_t)KEYLENID(pkey->ameth->pkey_id))
        return 0;

    *len = KEYLENID(pkey->ameth->pkey_id);
    memcpy(priv, key->privkey, *len);

    return 1;
}

static int ecx_get_pub_key(const EVP_PKEY *pkey, unsigned char *pub,
                           size_t *len)
{
    const ECX_KEY *key = pkey->pkey.ecx;

    if (pub == NULL) {
        *len = KEYLENID(pkey->ameth->pkey_id);
        return 1;
    }

    if (key == NULL
            || *len < (size_t)KEYLENID(pkey->ameth->pkey_id))
        return 0;

    *len = KEYLENID(pkey->ameth->pkey_id);
    memcpy(pub, key->pubkey, *len);

    return 1;
}

const EVP_PKEY_ASN1_METHOD ecx25519_asn1_meth = {
    EVP_PKEY_X25519,
    EVP_PKEY_X25519,
    0,
    "X25519",
    "OpenSSL X25519 algorithm",

    ecx_pub_decode,
    ecx_pub_encode,
    ecx_pub_cmp,
    ecx_pub_print,

    ecx_priv_decode,
    ecx_priv_encode,
    ecx_priv_print,

    ecx_size,
    ecx_bits,
    ecx_security_bits,

    0, 0, 0, 0,
    ecx_cmp_parameters,
    0, 0,

    ecx_free,
    ecx_ctrl,
    NULL,
    NULL,

    NULL,
    NULL,
    NULL,

    NULL,
    NULL,
    NULL,

    ecx_set_priv_key,
    ecx_set_pub_key,
    ecx_get_priv_key,
    ecx_get_pub_key,
};

const EVP_PKEY_ASN1_METHOD ecx448_asn1_meth = {
    EVP_PKEY_X448,
    EVP_PKEY_X448,
    0,
    "X448",
    "OpenSSL X448 algorithm",

    ecx_pub_decode,
    ecx_pub_encode,
    ecx_pub_cmp,
    ecx_pub_print,

    ecx_priv_decode,
    ecx_priv_encode,
    ecx_priv_print,

    ecx_size,
    ecx_bits,
    ecx_security_bits,

    0, 0, 0, 0,
    ecx_cmp_parameters,
    0, 0,

    ecx_free,
    ecx_ctrl,
    NULL,
    NULL,

    NULL,
    NULL,
    NULL,

    NULL,
    NULL,
    NULL,

    ecx_set_priv_key,
    ecx_set_pub_key,
    ecx_get_priv_key,
    ecx_get_pub_key,
};

static int ecd_size25519(const EVP_PKEY *pkey)
{
    return ED25519_SIGSIZE;
}

static int ecd_size448(const EVP_PKEY *pkey)
{
    return ED448_SIGSIZE;
}

static int ecd_item_verify(EVP_MD_CTX *ctx, const ASN1_ITEM *it, void *asn,
                           X509_ALGOR *sigalg, ASN1_BIT_STRING *str,
                           EVP_PKEY *pkey)
{
    const ASN1_OBJECT *obj;
    int ptype;
    int nid;

    /* Sanity check: make sure it is ED25519/ED448 with absent parameters */
    X509_ALGOR_get0(&obj, &ptype, NULL, sigalg);
    nid = OBJ_obj2nid(obj);
    if ((nid != NID_ED25519 && nid != NID_ED448) || ptype != V_ASN1_UNDEF) {
        ECerr(EC_F_ECD_ITEM_VERIFY, EC_R_INVALID_ENCODING);
        return 0;
    }

    if (!EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pkey))
        return 0;

    return 2;
}

static int ecd_item_sign25519(EVP_MD_CTX *ctx, const ASN1_ITEM *it, void *asn,
                              X509_ALGOR *alg1, X509_ALGOR *alg2,
                              ASN1_BIT_STRING *str)
{
    /* Set algorithms identifiers */
    X509_ALGOR_set0(alg1, OBJ_nid2obj(NID_ED25519), V_ASN1_UNDEF, NULL);
    if (alg2)
        X509_ALGOR_set0(alg2, OBJ_nid2obj(NID_ED25519), V_ASN1_UNDEF, NULL);
    /* Algorithm idetifiers set: carry on as normal */
    return 3;
}

static int ecd_sig_info_set25519(X509_SIG_INFO *siginf, const X509_ALGOR *alg,
                                 const ASN1_STRING *sig)
{
    X509_SIG_INFO_set(siginf, NID_undef, NID_ED25519, X25519_SECURITY_BITS,
                      X509_SIG_INFO_TLS);
    return 1;
}

static int ecd_item_sign448(EVP_MD_CTX *ctx, const ASN1_ITEM *it, void *asn,
                            X509_ALGOR *alg1, X509_ALGOR *alg2,
                            ASN1_BIT_STRING *str)
{
    /* Set algorithm identifier */
    X509_ALGOR_set0(alg1, OBJ_nid2obj(NID_ED448), V_ASN1_UNDEF, NULL);
    if (alg2 != NULL)
        X509_ALGOR_set0(alg2, OBJ_nid2obj(NID_ED448), V_ASN1_UNDEF, NULL);
    /* Algorithm identifier set: carry on as normal */
    return 3;
}

static int ecd_sig_info_set448(X509_SIG_INFO *siginf, const X509_ALGOR *alg,
                               const ASN1_STRING *sig)
{
    X509_SIG_INFO_set(siginf, NID_undef, NID_ED448, X448_SECURITY_BITS,
                      X509_SIG_INFO_TLS);
    return 1;
}


const EVP_PKEY_ASN1_METHOD ed25519_asn1_meth = {
    EVP_PKEY_ED25519,
    EVP_PKEY_ED25519,
    0,
    "ED25519",
    "OpenSSL ED25519 algorithm",

    ecx_pub_decode,
    ecx_pub_encode,
    ecx_pub_cmp,
    ecx_pub_print,

    ecx_priv_decode,
    ecx_priv_encode,
    ecx_priv_print,

    ecd_size25519,
    ecx_bits,
    ecx_security_bits,

    0, 0, 0, 0,
    ecx_cmp_parameters,
    0, 0,

    ecx_free,
    ecd_ctrl,
    NULL,
    NULL,
    ecd_item_verify,
    ecd_item_sign25519,
    ecd_sig_info_set25519,

    NULL,
    NULL,
    NULL,

    ecx_set_priv_key,
    ecx_set_pub_key,
    ecx_get_priv_key,
    ecx_get_pub_key,
};

const EVP_PKEY_ASN1_METHOD ed448_asn1_meth = {
    EVP_PKEY_ED448,
    EVP_PKEY_ED448,
    0,
    "ED448",
    "OpenSSL ED448 algorithm",

    ecx_pub_decode,
    ecx_pub_encode,
    ecx_pub_cmp,
    ecx_pub_print,

    ecx_priv_decode,
    ecx_priv_encode,
    ecx_priv_print,

    ecd_size448,
    ecx_bits,
    ecx_security_bits,

    0, 0, 0, 0,
    ecx_cmp_parameters,
    0, 0,

    ecx_free,
    ecd_ctrl,
    NULL,
    NULL,
    ecd_item_verify,
    ecd_item_sign448,
    ecd_sig_info_set448,

    NULL,
    NULL,
    NULL,

    ecx_set_priv_key,
    ecx_set_pub_key,
    ecx_get_priv_key,
    ecx_get_pub_key,
};

static int pkey_ecx_keygen(EVP_PKEY_CTX *ctx, EVP_PKEY *pkey)
{
    return ecx_key_op(pkey, ctx->pmeth->pkey_id, NULL, NULL, 0, KEY_OP_KEYGEN);
}

static int validate_ecx_derive(EVP_PKEY_CTX *ctx, unsigned char *key,
                                          size_t *keylen,
                                          const unsigned char **privkey,
                                          const unsigned char **pubkey)
{
    const ECX_KEY *ecxkey, *peerkey;

    if (ctx->pkey == NULL || ctx->peerkey == NULL) {
        ECerr(EC_F_VALIDATE_ECX_DERIVE, EC_R_KEYS_NOT_SET);
        return 0;
    }
    ecxkey = ctx->pkey->pkey.ecx;
    peerkey = ctx->peerkey->pkey.ecx;
    if (ecxkey == NULL || ecxkey->privkey == NULL) {
        ECerr(EC_F_VALIDATE_ECX_DERIVE, EC_R_INVALID_PRIVATE_KEY);
        return 0;
    }
    if (peerkey == NULL) {
        ECerr(EC_F_VALIDATE_ECX_DERIVE, EC_R_INVALID_PEER_KEY);
        return 0;
    }
    *privkey = ecxkey->privkey;
    *pubkey = peerkey->pubkey;

    return 1;
}

static int pkey_ecx_derive25519(EVP_PKEY_CTX *ctx, unsigned char *key,
                                size_t *keylen)
{
    const unsigned char *privkey, *pubkey;

    if (!validate_ecx_derive(ctx, key, keylen, &privkey, &pubkey)
            || (key != NULL
                && X25519(key, privkey, pubkey) == 0))
        return 0;
    *keylen = X25519_KEYLEN;
    return 1;
}

static int pkey_ecx_derive448(EVP_PKEY_CTX *ctx, unsigned char *key,
                              size_t *keylen)
{
    const unsigned char *privkey, *pubkey;

    if (!validate_ecx_derive(ctx, key, keylen, &privkey, &pubkey)
            || (key != NULL
                && X448(key, privkey, pubkey) == 0))
        return 0;
    *keylen = X448_KEYLEN;
    return 1;
}

static int pkey_ecx_ctrl(EVP_PKEY_CTX *ctx, int type, int p1, void *p2)
{
    /* Only need to handle peer key for derivation */
    if (type == EVP_PKEY_CTRL_PEER_KEY)
        return 1;
    return -2;
}

const EVP_PKEY_METHOD ecx25519_pkey_meth = {
    EVP_PKEY_X25519,
    0, 0, 0, 0, 0, 0, 0,
    pkey_ecx_keygen,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    pkey_ecx_derive25519,
    pkey_ecx_ctrl,
    0
};

const EVP_PKEY_METHOD ecx448_pkey_meth = {
    EVP_PKEY_X448,
    0, 0, 0, 0, 0, 0, 0,
    pkey_ecx_keygen,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    pkey_ecx_derive448,
    pkey_ecx_ctrl,
    0
};

static int pkey_ecd_digestsign25519(EVP_MD_CTX *ctx, unsigned char *sig,
                                    size_t *siglen, const unsigned char *tbs,
                                    size_t tbslen)
{
    const ECX_KEY *edkey = EVP_MD_CTX_pkey_ctx(ctx)->pkey->pkey.ecx;

    if (sig == NULL) {
        *siglen = ED25519_SIGSIZE;
        return 1;
    }
    if (*siglen < ED25519_SIGSIZE) {
        ECerr(EC_F_PKEY_ECD_DIGESTSIGN25519, EC_R_BUFFER_TOO_SMALL);
        return 0;
    }

    if (ED25519_sign(sig, tbs, tbslen, edkey->pubkey, edkey->privkey) == 0)
        return 0;
    *siglen = ED25519_SIGSIZE;
    return 1;
}

static int pkey_ecd_digestsign448(EVP_MD_CTX *ctx, unsigned char *sig,
                                  size_t *siglen, const unsigned char *tbs,
                                  size_t tbslen)
{
    const ECX_KEY *edkey = EVP_MD_CTX_pkey_ctx(ctx)->pkey->pkey.ecx;

    if (sig == NULL) {
        *siglen = ED448_SIGSIZE;
        return 1;
    }
    if (*siglen < ED448_SIGSIZE) {
        ECerr(EC_F_PKEY_ECD_DIGESTSIGN448, EC_R_BUFFER_TOO_SMALL);
        return 0;
    }

    if (ED448_sign(sig, tbs, tbslen, edkey->pubkey, edkey->privkey, NULL,
                   0) == 0)
        return 0;
    *siglen = ED448_SIGSIZE;
    return 1;
}

static int pkey_ecd_digestverify25519(EVP_MD_CTX *ctx, const unsigned char *sig,
                                      size_t siglen, const unsigned char *tbs,
                                      size_t tbslen)
{
    const ECX_KEY *edkey = EVP_MD_CTX_pkey_ctx(ctx)->pkey->pkey.ecx;

    if (siglen != ED25519_SIGSIZE)
        return 0;

    return ED25519_verify(tbs, tbslen, sig, edkey->pubkey);
}

static int pkey_ecd_digestverify448(EVP_MD_CTX *ctx, const unsigned char *sig,
                                    size_t siglen, const unsigned char *tbs,
                                    size_t tbslen)
{
    const ECX_KEY *edkey = EVP_MD_CTX_pkey_ctx(ctx)->pkey->pkey.ecx;

    if (siglen != ED448_SIGSIZE)
        return 0;

    return ED448_verify(tbs, tbslen, sig, edkey->pubkey, NULL, 0);
}

static int pkey_ecd_ctrl(EVP_PKEY_CTX *ctx, int type, int p1, void *p2)
{
    switch (type) {
    case EVP_PKEY_CTRL_MD:
        /* Only NULL allowed as digest */
        if (p2 == NULL || (const EVP_MD *)p2 == EVP_md_null())
            return 1;
        ECerr(EC_F_PKEY_ECD_CTRL, EC_R_INVALID_DIGEST_TYPE);
        return 0;

    case EVP_PKEY_CTRL_DIGESTINIT:
        return 1;
    }
    return -2;
}

const EVP_PKEY_METHOD ed25519_pkey_meth = {
    EVP_PKEY_ED25519, EVP_PKEY_FLAG_SIGCTX_CUSTOM,
    0, 0, 0, 0, 0, 0,
    pkey_ecx_keygen,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    pkey_ecd_ctrl,
    0,
    pkey_ecd_digestsign25519,
    pkey_ecd_digestverify25519
};

const EVP_PKEY_METHOD ed448_pkey_meth = {
    EVP_PKEY_ED448, EVP_PKEY_FLAG_SIGCTX_CUSTOM,
    0, 0, 0, 0, 0, 0,
    pkey_ecx_keygen,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    pkey_ecd_ctrl,
    0,
    pkey_ecd_digestsign448,
    pkey_ecd_digestverify448
};
