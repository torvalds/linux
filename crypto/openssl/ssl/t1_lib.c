/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <openssl/objects.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/ocsp.h>
#include <openssl/conf.h>
#include <openssl/x509v3.h>
#include <openssl/dh.h>
#include <openssl/bn.h>
#include "internal/nelem.h"
#include "ssl_locl.h"
#include <openssl/ct.h>

SSL3_ENC_METHOD const TLSv1_enc_data = {
    tls1_enc,
    tls1_mac,
    tls1_setup_key_block,
    tls1_generate_master_secret,
    tls1_change_cipher_state,
    tls1_final_finish_mac,
    TLS_MD_CLIENT_FINISH_CONST, TLS_MD_CLIENT_FINISH_CONST_SIZE,
    TLS_MD_SERVER_FINISH_CONST, TLS_MD_SERVER_FINISH_CONST_SIZE,
    tls1_alert_code,
    tls1_export_keying_material,
    0,
    ssl3_set_handshake_header,
    tls_close_construct_packet,
    ssl3_handshake_write
};

SSL3_ENC_METHOD const TLSv1_1_enc_data = {
    tls1_enc,
    tls1_mac,
    tls1_setup_key_block,
    tls1_generate_master_secret,
    tls1_change_cipher_state,
    tls1_final_finish_mac,
    TLS_MD_CLIENT_FINISH_CONST, TLS_MD_CLIENT_FINISH_CONST_SIZE,
    TLS_MD_SERVER_FINISH_CONST, TLS_MD_SERVER_FINISH_CONST_SIZE,
    tls1_alert_code,
    tls1_export_keying_material,
    SSL_ENC_FLAG_EXPLICIT_IV,
    ssl3_set_handshake_header,
    tls_close_construct_packet,
    ssl3_handshake_write
};

SSL3_ENC_METHOD const TLSv1_2_enc_data = {
    tls1_enc,
    tls1_mac,
    tls1_setup_key_block,
    tls1_generate_master_secret,
    tls1_change_cipher_state,
    tls1_final_finish_mac,
    TLS_MD_CLIENT_FINISH_CONST, TLS_MD_CLIENT_FINISH_CONST_SIZE,
    TLS_MD_SERVER_FINISH_CONST, TLS_MD_SERVER_FINISH_CONST_SIZE,
    tls1_alert_code,
    tls1_export_keying_material,
    SSL_ENC_FLAG_EXPLICIT_IV | SSL_ENC_FLAG_SIGALGS | SSL_ENC_FLAG_SHA256_PRF
        | SSL_ENC_FLAG_TLS1_2_CIPHERS,
    ssl3_set_handshake_header,
    tls_close_construct_packet,
    ssl3_handshake_write
};

SSL3_ENC_METHOD const TLSv1_3_enc_data = {
    tls13_enc,
    tls1_mac,
    tls13_setup_key_block,
    tls13_generate_master_secret,
    tls13_change_cipher_state,
    tls13_final_finish_mac,
    TLS_MD_CLIENT_FINISH_CONST, TLS_MD_CLIENT_FINISH_CONST_SIZE,
    TLS_MD_SERVER_FINISH_CONST, TLS_MD_SERVER_FINISH_CONST_SIZE,
    tls13_alert_code,
    tls13_export_keying_material,
    SSL_ENC_FLAG_SIGALGS | SSL_ENC_FLAG_SHA256_PRF,
    ssl3_set_handshake_header,
    tls_close_construct_packet,
    ssl3_handshake_write
};

long tls1_default_timeout(void)
{
    /*
     * 2 hours, the 24 hours mentioned in the TLSv1 spec is way too long for
     * http, the cache would over fill
     */
    return (60 * 60 * 2);
}

int tls1_new(SSL *s)
{
    if (!ssl3_new(s))
        return 0;
    if (!s->method->ssl_clear(s))
        return 0;

    return 1;
}

void tls1_free(SSL *s)
{
    OPENSSL_free(s->ext.session_ticket);
    ssl3_free(s);
}

int tls1_clear(SSL *s)
{
    if (!ssl3_clear(s))
        return 0;

    if (s->method->version == TLS_ANY_VERSION)
        s->version = TLS_MAX_VERSION;
    else
        s->version = s->method->version;

    return 1;
}

#ifndef OPENSSL_NO_EC

/*
 * Table of curve information.
 * Do not delete entries or reorder this array! It is used as a lookup
 * table: the index of each entry is one less than the TLS curve id.
 */
static const TLS_GROUP_INFO nid_list[] = {
    {NID_sect163k1, 80, TLS_CURVE_CHAR2}, /* sect163k1 (1) */
    {NID_sect163r1, 80, TLS_CURVE_CHAR2}, /* sect163r1 (2) */
    {NID_sect163r2, 80, TLS_CURVE_CHAR2}, /* sect163r2 (3) */
    {NID_sect193r1, 80, TLS_CURVE_CHAR2}, /* sect193r1 (4) */
    {NID_sect193r2, 80, TLS_CURVE_CHAR2}, /* sect193r2 (5) */
    {NID_sect233k1, 112, TLS_CURVE_CHAR2}, /* sect233k1 (6) */
    {NID_sect233r1, 112, TLS_CURVE_CHAR2}, /* sect233r1 (7) */
    {NID_sect239k1, 112, TLS_CURVE_CHAR2}, /* sect239k1 (8) */
    {NID_sect283k1, 128, TLS_CURVE_CHAR2}, /* sect283k1 (9) */
    {NID_sect283r1, 128, TLS_CURVE_CHAR2}, /* sect283r1 (10) */
    {NID_sect409k1, 192, TLS_CURVE_CHAR2}, /* sect409k1 (11) */
    {NID_sect409r1, 192, TLS_CURVE_CHAR2}, /* sect409r1 (12) */
    {NID_sect571k1, 256, TLS_CURVE_CHAR2}, /* sect571k1 (13) */
    {NID_sect571r1, 256, TLS_CURVE_CHAR2}, /* sect571r1 (14) */
    {NID_secp160k1, 80, TLS_CURVE_PRIME}, /* secp160k1 (15) */
    {NID_secp160r1, 80, TLS_CURVE_PRIME}, /* secp160r1 (16) */
    {NID_secp160r2, 80, TLS_CURVE_PRIME}, /* secp160r2 (17) */
    {NID_secp192k1, 80, TLS_CURVE_PRIME}, /* secp192k1 (18) */
    {NID_X9_62_prime192v1, 80, TLS_CURVE_PRIME}, /* secp192r1 (19) */
    {NID_secp224k1, 112, TLS_CURVE_PRIME}, /* secp224k1 (20) */
    {NID_secp224r1, 112, TLS_CURVE_PRIME}, /* secp224r1 (21) */
    {NID_secp256k1, 128, TLS_CURVE_PRIME}, /* secp256k1 (22) */
    {NID_X9_62_prime256v1, 128, TLS_CURVE_PRIME}, /* secp256r1 (23) */
    {NID_secp384r1, 192, TLS_CURVE_PRIME}, /* secp384r1 (24) */
    {NID_secp521r1, 256, TLS_CURVE_PRIME}, /* secp521r1 (25) */
    {NID_brainpoolP256r1, 128, TLS_CURVE_PRIME}, /* brainpoolP256r1 (26) */
    {NID_brainpoolP384r1, 192, TLS_CURVE_PRIME}, /* brainpoolP384r1 (27) */
    {NID_brainpoolP512r1, 256, TLS_CURVE_PRIME}, /* brainpool512r1 (28) */
    {EVP_PKEY_X25519, 128, TLS_CURVE_CUSTOM}, /* X25519 (29) */
    {EVP_PKEY_X448, 224, TLS_CURVE_CUSTOM}, /* X448 (30) */
};

static const unsigned char ecformats_default[] = {
    TLSEXT_ECPOINTFORMAT_uncompressed,
    TLSEXT_ECPOINTFORMAT_ansiX962_compressed_prime,
    TLSEXT_ECPOINTFORMAT_ansiX962_compressed_char2
};

/* The default curves */
static const uint16_t eccurves_default[] = {
    29,                      /* X25519 (29) */
    23,                      /* secp256r1 (23) */
    30,                      /* X448 (30) */
    25,                      /* secp521r1 (25) */
    24,                      /* secp384r1 (24) */
};

static const uint16_t suiteb_curves[] = {
    TLSEXT_curve_P_256,
    TLSEXT_curve_P_384
};

const TLS_GROUP_INFO *tls1_group_id_lookup(uint16_t group_id)
{
    /* ECC curves from RFC 4492 and RFC 7027 */
    if (group_id < 1 || group_id > OSSL_NELEM(nid_list))
        return NULL;
    return &nid_list[group_id - 1];
}

static uint16_t tls1_nid2group_id(int nid)
{
    size_t i;
    for (i = 0; i < OSSL_NELEM(nid_list); i++) {
        if (nid_list[i].nid == nid)
            return (uint16_t)(i + 1);
    }
    return 0;
}

/*
 * Set *pgroups to the supported groups list and *pgroupslen to
 * the number of groups supported.
 */
void tls1_get_supported_groups(SSL *s, const uint16_t **pgroups,
                               size_t *pgroupslen)
{

    /* For Suite B mode only include P-256, P-384 */
    switch (tls1_suiteb(s)) {
    case SSL_CERT_FLAG_SUITEB_128_LOS:
        *pgroups = suiteb_curves;
        *pgroupslen = OSSL_NELEM(suiteb_curves);
        break;

    case SSL_CERT_FLAG_SUITEB_128_LOS_ONLY:
        *pgroups = suiteb_curves;
        *pgroupslen = 1;
        break;

    case SSL_CERT_FLAG_SUITEB_192_LOS:
        *pgroups = suiteb_curves + 1;
        *pgroupslen = 1;
        break;

    default:
        if (s->ext.supportedgroups == NULL) {
            *pgroups = eccurves_default;
            *pgroupslen = OSSL_NELEM(eccurves_default);
        } else {
            *pgroups = s->ext.supportedgroups;
            *pgroupslen = s->ext.supportedgroups_len;
        }
        break;
    }
}

/* See if curve is allowed by security callback */
int tls_curve_allowed(SSL *s, uint16_t curve, int op)
{
    const TLS_GROUP_INFO *cinfo = tls1_group_id_lookup(curve);
    unsigned char ctmp[2];

    if (cinfo == NULL)
        return 0;
# ifdef OPENSSL_NO_EC2M
    if (cinfo->flags & TLS_CURVE_CHAR2)
        return 0;
# endif
    ctmp[0] = curve >> 8;
    ctmp[1] = curve & 0xff;
    return ssl_security(s, op, cinfo->secbits, cinfo->nid, (void *)ctmp);
}

/* Return 1 if "id" is in "list" */
static int tls1_in_list(uint16_t id, const uint16_t *list, size_t listlen)
{
    size_t i;
    for (i = 0; i < listlen; i++)
        if (list[i] == id)
            return 1;
    return 0;
}

/*-
 * For nmatch >= 0, return the id of the |nmatch|th shared group or 0
 * if there is no match.
 * For nmatch == -1, return number of matches
 * For nmatch == -2, return the id of the group to use for
 * a tmp key, or 0 if there is no match.
 */
uint16_t tls1_shared_group(SSL *s, int nmatch)
{
    const uint16_t *pref, *supp;
    size_t num_pref, num_supp, i;
    int k;

    /* Can't do anything on client side */
    if (s->server == 0)
        return 0;
    if (nmatch == -2) {
        if (tls1_suiteb(s)) {
            /*
             * For Suite B ciphersuite determines curve: we already know
             * these are acceptable due to previous checks.
             */
            unsigned long cid = s->s3->tmp.new_cipher->id;

            if (cid == TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256)
                return TLSEXT_curve_P_256;
            if (cid == TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384)
                return TLSEXT_curve_P_384;
            /* Should never happen */
            return 0;
        }
        /* If not Suite B just return first preference shared curve */
        nmatch = 0;
    }
    /*
     * If server preference set, our groups are the preference order
     * otherwise peer decides.
     */
    if (s->options & SSL_OP_CIPHER_SERVER_PREFERENCE) {
        tls1_get_supported_groups(s, &pref, &num_pref);
        tls1_get_peer_groups(s, &supp, &num_supp);
    } else {
        tls1_get_peer_groups(s, &pref, &num_pref);
        tls1_get_supported_groups(s, &supp, &num_supp);
    }

    for (k = 0, i = 0; i < num_pref; i++) {
        uint16_t id = pref[i];

        if (!tls1_in_list(id, supp, num_supp)
            || !tls_curve_allowed(s, id, SSL_SECOP_CURVE_SHARED))
                    continue;
        if (nmatch == k)
            return id;
         k++;
    }
    if (nmatch == -1)
        return k;
    /* Out of range (nmatch > k). */
    return 0;
}

int tls1_set_groups(uint16_t **pext, size_t *pextlen,
                    int *groups, size_t ngroups)
{
    uint16_t *glist;
    size_t i;
    /*
     * Bitmap of groups included to detect duplicates: only works while group
     * ids < 32
     */
    unsigned long dup_list = 0;

    if (ngroups == 0) {
        SSLerr(SSL_F_TLS1_SET_GROUPS, SSL_R_BAD_LENGTH);
        return 0;
    }
    if ((glist = OPENSSL_malloc(ngroups * sizeof(*glist))) == NULL) {
        SSLerr(SSL_F_TLS1_SET_GROUPS, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    for (i = 0; i < ngroups; i++) {
        unsigned long idmask;
        uint16_t id;
        /* TODO(TLS1.3): Convert for DH groups */
        id = tls1_nid2group_id(groups[i]);
        idmask = 1L << id;
        if (!id || (dup_list & idmask)) {
            OPENSSL_free(glist);
            return 0;
        }
        dup_list |= idmask;
        glist[i] = id;
    }
    OPENSSL_free(*pext);
    *pext = glist;
    *pextlen = ngroups;
    return 1;
}

# define MAX_CURVELIST   OSSL_NELEM(nid_list)

typedef struct {
    size_t nidcnt;
    int nid_arr[MAX_CURVELIST];
} nid_cb_st;

static int nid_cb(const char *elem, int len, void *arg)
{
    nid_cb_st *narg = arg;
    size_t i;
    int nid;
    char etmp[20];
    if (elem == NULL)
        return 0;
    if (narg->nidcnt == MAX_CURVELIST)
        return 0;
    if (len > (int)(sizeof(etmp) - 1))
        return 0;
    memcpy(etmp, elem, len);
    etmp[len] = 0;
    nid = EC_curve_nist2nid(etmp);
    if (nid == NID_undef)
        nid = OBJ_sn2nid(etmp);
    if (nid == NID_undef)
        nid = OBJ_ln2nid(etmp);
    if (nid == NID_undef)
        return 0;
    for (i = 0; i < narg->nidcnt; i++)
        if (narg->nid_arr[i] == nid)
            return 0;
    narg->nid_arr[narg->nidcnt++] = nid;
    return 1;
}

/* Set groups based on a colon separate list */
int tls1_set_groups_list(uint16_t **pext, size_t *pextlen, const char *str)
{
    nid_cb_st ncb;
    ncb.nidcnt = 0;
    if (!CONF_parse_list(str, ':', 1, nid_cb, &ncb))
        return 0;
    if (pext == NULL)
        return 1;
    return tls1_set_groups(pext, pextlen, ncb.nid_arr, ncb.nidcnt);
}
/* Return group id of a key */
static uint16_t tls1_get_group_id(EVP_PKEY *pkey)
{
    EC_KEY *ec = EVP_PKEY_get0_EC_KEY(pkey);
    const EC_GROUP *grp;

    if (ec == NULL)
        return 0;
    grp = EC_KEY_get0_group(ec);
    return tls1_nid2group_id(EC_GROUP_get_curve_name(grp));
}

/* Check a key is compatible with compression extension */
static int tls1_check_pkey_comp(SSL *s, EVP_PKEY *pkey)
{
    const EC_KEY *ec;
    const EC_GROUP *grp;
    unsigned char comp_id;
    size_t i;

    /* If not an EC key nothing to check */
    if (EVP_PKEY_id(pkey) != EVP_PKEY_EC)
        return 1;
    ec = EVP_PKEY_get0_EC_KEY(pkey);
    grp = EC_KEY_get0_group(ec);

    /* Get required compression id */
    if (EC_KEY_get_conv_form(ec) == POINT_CONVERSION_UNCOMPRESSED) {
            comp_id = TLSEXT_ECPOINTFORMAT_uncompressed;
    } else if (SSL_IS_TLS13(s)) {
            /*
             * ec_point_formats extension is not used in TLSv1.3 so we ignore
             * this check.
             */
            return 1;
    } else {
        int field_type = EC_METHOD_get_field_type(EC_GROUP_method_of(grp));

        if (field_type == NID_X9_62_prime_field)
            comp_id = TLSEXT_ECPOINTFORMAT_ansiX962_compressed_prime;
        else if (field_type == NID_X9_62_characteristic_two_field)
            comp_id = TLSEXT_ECPOINTFORMAT_ansiX962_compressed_char2;
        else
            return 0;
    }
    /*
     * If point formats extension present check it, otherwise everything is
     * supported (see RFC4492).
     */
    if (s->session->ext.ecpointformats == NULL)
        return 1;

    for (i = 0; i < s->session->ext.ecpointformats_len; i++) {
        if (s->session->ext.ecpointformats[i] == comp_id)
            return 1;
    }
    return 0;
}

/* Check a group id matches preferences */
int tls1_check_group_id(SSL *s, uint16_t group_id, int check_own_groups)
    {
    const uint16_t *groups;
    size_t groups_len;

    if (group_id == 0)
        return 0;

    /* Check for Suite B compliance */
    if (tls1_suiteb(s) && s->s3->tmp.new_cipher != NULL) {
        unsigned long cid = s->s3->tmp.new_cipher->id;

        if (cid == TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256) {
            if (group_id != TLSEXT_curve_P_256)
                return 0;
        } else if (cid == TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384) {
            if (group_id != TLSEXT_curve_P_384)
                return 0;
        } else {
            /* Should never happen */
            return 0;
        }
    }

    if (check_own_groups) {
        /* Check group is one of our preferences */
        tls1_get_supported_groups(s, &groups, &groups_len);
        if (!tls1_in_list(group_id, groups, groups_len))
            return 0;
    }

    if (!tls_curve_allowed(s, group_id, SSL_SECOP_CURVE_CHECK))
        return 0;

    /* For clients, nothing more to check */
    if (!s->server)
        return 1;

    /* Check group is one of peers preferences */
    tls1_get_peer_groups(s, &groups, &groups_len);

    /*
     * RFC 4492 does not require the supported elliptic curves extension
     * so if it is not sent we can just choose any curve.
     * It is invalid to send an empty list in the supported groups
     * extension, so groups_len == 0 always means no extension.
     */
    if (groups_len == 0)
            return 1;
    return tls1_in_list(group_id, groups, groups_len);
}

void tls1_get_formatlist(SSL *s, const unsigned char **pformats,
                         size_t *num_formats)
{
    /*
     * If we have a custom point format list use it otherwise use default
     */
    if (s->ext.ecpointformats) {
        *pformats = s->ext.ecpointformats;
        *num_formats = s->ext.ecpointformats_len;
    } else {
        *pformats = ecformats_default;
        /* For Suite B we don't support char2 fields */
        if (tls1_suiteb(s))
            *num_formats = sizeof(ecformats_default) - 1;
        else
            *num_formats = sizeof(ecformats_default);
    }
}

/*
 * Check cert parameters compatible with extensions: currently just checks EC
 * certificates have compatible curves and compression.
 */
static int tls1_check_cert_param(SSL *s, X509 *x, int check_ee_md)
{
    uint16_t group_id;
    EVP_PKEY *pkey;
    pkey = X509_get0_pubkey(x);
    if (pkey == NULL)
        return 0;
    /* If not EC nothing to do */
    if (EVP_PKEY_id(pkey) != EVP_PKEY_EC)
        return 1;
    /* Check compression */
    if (!tls1_check_pkey_comp(s, pkey))
        return 0;
    group_id = tls1_get_group_id(pkey);
    /*
     * For a server we allow the certificate to not be in our list of supported
     * groups.
     */
    if (!tls1_check_group_id(s, group_id, !s->server))
        return 0;
    /*
     * Special case for suite B. We *MUST* sign using SHA256+P-256 or
     * SHA384+P-384.
     */
    if (check_ee_md && tls1_suiteb(s)) {
        int check_md;
        size_t i;
        CERT *c = s->cert;

        /* Check to see we have necessary signing algorithm */
        if (group_id == TLSEXT_curve_P_256)
            check_md = NID_ecdsa_with_SHA256;
        else if (group_id == TLSEXT_curve_P_384)
            check_md = NID_ecdsa_with_SHA384;
        else
            return 0;           /* Should never happen */
        for (i = 0; i < c->shared_sigalgslen; i++) {
            if (check_md == c->shared_sigalgs[i]->sigandhash)
                return 1;;
        }
        return 0;
    }
    return 1;
}

/*
 * tls1_check_ec_tmp_key - Check EC temporary key compatibility
 * @s: SSL connection
 * @cid: Cipher ID we're considering using
 *
 * Checks that the kECDHE cipher suite we're considering using
 * is compatible with the client extensions.
 *
 * Returns 0 when the cipher can't be used or 1 when it can.
 */
int tls1_check_ec_tmp_key(SSL *s, unsigned long cid)
{
    /* If not Suite B just need a shared group */
    if (!tls1_suiteb(s))
        return tls1_shared_group(s, 0) != 0;
    /*
     * If Suite B, AES128 MUST use P-256 and AES256 MUST use P-384, no other
     * curves permitted.
     */
    if (cid == TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256)
        return tls1_check_group_id(s, TLSEXT_curve_P_256, 1);
    if (cid == TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384)
        return tls1_check_group_id(s, TLSEXT_curve_P_384, 1);

    return 0;
}

#else

static int tls1_check_cert_param(SSL *s, X509 *x, int set_ee_md)
{
    return 1;
}

#endif                          /* OPENSSL_NO_EC */

/* Default sigalg schemes */
static const uint16_t tls12_sigalgs[] = {
#ifndef OPENSSL_NO_EC
    TLSEXT_SIGALG_ecdsa_secp256r1_sha256,
    TLSEXT_SIGALG_ecdsa_secp384r1_sha384,
    TLSEXT_SIGALG_ecdsa_secp521r1_sha512,
    TLSEXT_SIGALG_ed25519,
    TLSEXT_SIGALG_ed448,
#endif

    TLSEXT_SIGALG_rsa_pss_pss_sha256,
    TLSEXT_SIGALG_rsa_pss_pss_sha384,
    TLSEXT_SIGALG_rsa_pss_pss_sha512,
    TLSEXT_SIGALG_rsa_pss_rsae_sha256,
    TLSEXT_SIGALG_rsa_pss_rsae_sha384,
    TLSEXT_SIGALG_rsa_pss_rsae_sha512,

    TLSEXT_SIGALG_rsa_pkcs1_sha256,
    TLSEXT_SIGALG_rsa_pkcs1_sha384,
    TLSEXT_SIGALG_rsa_pkcs1_sha512,

#ifndef OPENSSL_NO_EC
    TLSEXT_SIGALG_ecdsa_sha224,
    TLSEXT_SIGALG_ecdsa_sha1,
#endif
    TLSEXT_SIGALG_rsa_pkcs1_sha224,
    TLSEXT_SIGALG_rsa_pkcs1_sha1,
#ifndef OPENSSL_NO_DSA
    TLSEXT_SIGALG_dsa_sha224,
    TLSEXT_SIGALG_dsa_sha1,

    TLSEXT_SIGALG_dsa_sha256,
    TLSEXT_SIGALG_dsa_sha384,
    TLSEXT_SIGALG_dsa_sha512,
#endif
#ifndef OPENSSL_NO_GOST
    TLSEXT_SIGALG_gostr34102012_256_gostr34112012_256,
    TLSEXT_SIGALG_gostr34102012_512_gostr34112012_512,
    TLSEXT_SIGALG_gostr34102001_gostr3411,
#endif
};

#ifndef OPENSSL_NO_EC
static const uint16_t suiteb_sigalgs[] = {
    TLSEXT_SIGALG_ecdsa_secp256r1_sha256,
    TLSEXT_SIGALG_ecdsa_secp384r1_sha384
};
#endif

static const SIGALG_LOOKUP sigalg_lookup_tbl[] = {
#ifndef OPENSSL_NO_EC
    {"ecdsa_secp256r1_sha256", TLSEXT_SIGALG_ecdsa_secp256r1_sha256,
     NID_sha256, SSL_MD_SHA256_IDX, EVP_PKEY_EC, SSL_PKEY_ECC,
     NID_ecdsa_with_SHA256, NID_X9_62_prime256v1},
    {"ecdsa_secp384r1_sha384", TLSEXT_SIGALG_ecdsa_secp384r1_sha384,
     NID_sha384, SSL_MD_SHA384_IDX, EVP_PKEY_EC, SSL_PKEY_ECC,
     NID_ecdsa_with_SHA384, NID_secp384r1},
    {"ecdsa_secp521r1_sha512", TLSEXT_SIGALG_ecdsa_secp521r1_sha512,
     NID_sha512, SSL_MD_SHA512_IDX, EVP_PKEY_EC, SSL_PKEY_ECC,
     NID_ecdsa_with_SHA512, NID_secp521r1},
    {"ed25519", TLSEXT_SIGALG_ed25519,
     NID_undef, -1, EVP_PKEY_ED25519, SSL_PKEY_ED25519,
     NID_undef, NID_undef},
    {"ed448", TLSEXT_SIGALG_ed448,
     NID_undef, -1, EVP_PKEY_ED448, SSL_PKEY_ED448,
     NID_undef, NID_undef},
    {NULL, TLSEXT_SIGALG_ecdsa_sha224,
     NID_sha224, SSL_MD_SHA224_IDX, EVP_PKEY_EC, SSL_PKEY_ECC,
     NID_ecdsa_with_SHA224, NID_undef},
    {NULL, TLSEXT_SIGALG_ecdsa_sha1,
     NID_sha1, SSL_MD_SHA1_IDX, EVP_PKEY_EC, SSL_PKEY_ECC,
     NID_ecdsa_with_SHA1, NID_undef},
#endif
    {"rsa_pss_rsae_sha256", TLSEXT_SIGALG_rsa_pss_rsae_sha256,
     NID_sha256, SSL_MD_SHA256_IDX, EVP_PKEY_RSA_PSS, SSL_PKEY_RSA,
     NID_undef, NID_undef},
    {"rsa_pss_rsae_sha384", TLSEXT_SIGALG_rsa_pss_rsae_sha384,
     NID_sha384, SSL_MD_SHA384_IDX, EVP_PKEY_RSA_PSS, SSL_PKEY_RSA,
     NID_undef, NID_undef},
    {"rsa_pss_rsae_sha512", TLSEXT_SIGALG_rsa_pss_rsae_sha512,
     NID_sha512, SSL_MD_SHA512_IDX, EVP_PKEY_RSA_PSS, SSL_PKEY_RSA,
     NID_undef, NID_undef},
    {"rsa_pss_pss_sha256", TLSEXT_SIGALG_rsa_pss_pss_sha256,
     NID_sha256, SSL_MD_SHA256_IDX, EVP_PKEY_RSA_PSS, SSL_PKEY_RSA_PSS_SIGN,
     NID_undef, NID_undef},
    {"rsa_pss_pss_sha384", TLSEXT_SIGALG_rsa_pss_pss_sha384,
     NID_sha384, SSL_MD_SHA384_IDX, EVP_PKEY_RSA_PSS, SSL_PKEY_RSA_PSS_SIGN,
     NID_undef, NID_undef},
    {"rsa_pss_pss_sha512", TLSEXT_SIGALG_rsa_pss_pss_sha512,
     NID_sha512, SSL_MD_SHA512_IDX, EVP_PKEY_RSA_PSS, SSL_PKEY_RSA_PSS_SIGN,
     NID_undef, NID_undef},
    {"rsa_pkcs1_sha256", TLSEXT_SIGALG_rsa_pkcs1_sha256,
     NID_sha256, SSL_MD_SHA256_IDX, EVP_PKEY_RSA, SSL_PKEY_RSA,
     NID_sha256WithRSAEncryption, NID_undef},
    {"rsa_pkcs1_sha384", TLSEXT_SIGALG_rsa_pkcs1_sha384,
     NID_sha384, SSL_MD_SHA384_IDX, EVP_PKEY_RSA, SSL_PKEY_RSA,
     NID_sha384WithRSAEncryption, NID_undef},
    {"rsa_pkcs1_sha512", TLSEXT_SIGALG_rsa_pkcs1_sha512,
     NID_sha512, SSL_MD_SHA512_IDX, EVP_PKEY_RSA, SSL_PKEY_RSA,
     NID_sha512WithRSAEncryption, NID_undef},
    {"rsa_pkcs1_sha224", TLSEXT_SIGALG_rsa_pkcs1_sha224,
     NID_sha224, SSL_MD_SHA224_IDX, EVP_PKEY_RSA, SSL_PKEY_RSA,
     NID_sha224WithRSAEncryption, NID_undef},
    {"rsa_pkcs1_sha1", TLSEXT_SIGALG_rsa_pkcs1_sha1,
     NID_sha1, SSL_MD_SHA1_IDX, EVP_PKEY_RSA, SSL_PKEY_RSA,
     NID_sha1WithRSAEncryption, NID_undef},
#ifndef OPENSSL_NO_DSA
    {NULL, TLSEXT_SIGALG_dsa_sha256,
     NID_sha256, SSL_MD_SHA256_IDX, EVP_PKEY_DSA, SSL_PKEY_DSA_SIGN,
     NID_dsa_with_SHA256, NID_undef},
    {NULL, TLSEXT_SIGALG_dsa_sha384,
     NID_sha384, SSL_MD_SHA384_IDX, EVP_PKEY_DSA, SSL_PKEY_DSA_SIGN,
     NID_undef, NID_undef},
    {NULL, TLSEXT_SIGALG_dsa_sha512,
     NID_sha512, SSL_MD_SHA512_IDX, EVP_PKEY_DSA, SSL_PKEY_DSA_SIGN,
     NID_undef, NID_undef},
    {NULL, TLSEXT_SIGALG_dsa_sha224,
     NID_sha224, SSL_MD_SHA224_IDX, EVP_PKEY_DSA, SSL_PKEY_DSA_SIGN,
     NID_undef, NID_undef},
    {NULL, TLSEXT_SIGALG_dsa_sha1,
     NID_sha1, SSL_MD_SHA1_IDX, EVP_PKEY_DSA, SSL_PKEY_DSA_SIGN,
     NID_dsaWithSHA1, NID_undef},
#endif
#ifndef OPENSSL_NO_GOST
    {NULL, TLSEXT_SIGALG_gostr34102012_256_gostr34112012_256,
     NID_id_GostR3411_2012_256, SSL_MD_GOST12_256_IDX,
     NID_id_GostR3410_2012_256, SSL_PKEY_GOST12_256,
     NID_undef, NID_undef},
    {NULL, TLSEXT_SIGALG_gostr34102012_512_gostr34112012_512,
     NID_id_GostR3411_2012_512, SSL_MD_GOST12_512_IDX,
     NID_id_GostR3410_2012_512, SSL_PKEY_GOST12_512,
     NID_undef, NID_undef},
    {NULL, TLSEXT_SIGALG_gostr34102001_gostr3411,
     NID_id_GostR3411_94, SSL_MD_GOST94_IDX,
     NID_id_GostR3410_2001, SSL_PKEY_GOST01,
     NID_undef, NID_undef}
#endif
};
/* Legacy sigalgs for TLS < 1.2 RSA TLS signatures */
static const SIGALG_LOOKUP legacy_rsa_sigalg = {
    "rsa_pkcs1_md5_sha1", 0,
     NID_md5_sha1, SSL_MD_MD5_SHA1_IDX,
     EVP_PKEY_RSA, SSL_PKEY_RSA,
     NID_undef, NID_undef
};

/*
 * Default signature algorithm values used if signature algorithms not present.
 * From RFC5246. Note: order must match certificate index order.
 */
static const uint16_t tls_default_sigalg[] = {
    TLSEXT_SIGALG_rsa_pkcs1_sha1, /* SSL_PKEY_RSA */
    0, /* SSL_PKEY_RSA_PSS_SIGN */
    TLSEXT_SIGALG_dsa_sha1, /* SSL_PKEY_DSA_SIGN */
    TLSEXT_SIGALG_ecdsa_sha1, /* SSL_PKEY_ECC */
    TLSEXT_SIGALG_gostr34102001_gostr3411, /* SSL_PKEY_GOST01 */
    TLSEXT_SIGALG_gostr34102012_256_gostr34112012_256, /* SSL_PKEY_GOST12_256 */
    TLSEXT_SIGALG_gostr34102012_512_gostr34112012_512, /* SSL_PKEY_GOST12_512 */
    0, /* SSL_PKEY_ED25519 */
    0, /* SSL_PKEY_ED448 */
};

/* Lookup TLS signature algorithm */
static const SIGALG_LOOKUP *tls1_lookup_sigalg(uint16_t sigalg)
{
    size_t i;
    const SIGALG_LOOKUP *s;

    for (i = 0, s = sigalg_lookup_tbl; i < OSSL_NELEM(sigalg_lookup_tbl);
         i++, s++) {
        if (s->sigalg == sigalg)
            return s;
    }
    return NULL;
}
/* Lookup hash: return 0 if invalid or not enabled */
int tls1_lookup_md(const SIGALG_LOOKUP *lu, const EVP_MD **pmd)
{
    const EVP_MD *md;
    if (lu == NULL)
        return 0;
    /* lu->hash == NID_undef means no associated digest */
    if (lu->hash == NID_undef) {
        md = NULL;
    } else {
        md = ssl_md(lu->hash_idx);
        if (md == NULL)
            return 0;
    }
    if (pmd)
        *pmd = md;
    return 1;
}

/*
 * Check if key is large enough to generate RSA-PSS signature.
 *
 * The key must greater than or equal to 2 * hash length + 2.
 * SHA512 has a hash length of 64 bytes, which is incompatible
 * with a 128 byte (1024 bit) key.
 */
#define RSA_PSS_MINIMUM_KEY_SIZE(md) (2 * EVP_MD_size(md) + 2)
static int rsa_pss_check_min_key_size(const RSA *rsa, const SIGALG_LOOKUP *lu)
{
    const EVP_MD *md;

    if (rsa == NULL)
        return 0;
    if (!tls1_lookup_md(lu, &md) || md == NULL)
        return 0;
    if (RSA_size(rsa) < RSA_PSS_MINIMUM_KEY_SIZE(md))
        return 0;
    return 1;
}

/*
 * Return a signature algorithm for TLS < 1.2 where the signature type
 * is fixed by the certificate type.
 */
static const SIGALG_LOOKUP *tls1_get_legacy_sigalg(const SSL *s, int idx)
{
    if (idx == -1) {
        if (s->server) {
            size_t i;

            /* Work out index corresponding to ciphersuite */
            for (i = 0; i < SSL_PKEY_NUM; i++) {
                const SSL_CERT_LOOKUP *clu = ssl_cert_lookup_by_idx(i);

                if (clu->amask & s->s3->tmp.new_cipher->algorithm_auth) {
                    idx = i;
                    break;
                }
            }

            /*
             * Some GOST ciphersuites allow more than one signature algorithms
             * */
            if (idx == SSL_PKEY_GOST01 && s->s3->tmp.new_cipher->algorithm_auth != SSL_aGOST01) {
                int real_idx;

                for (real_idx = SSL_PKEY_GOST12_512; real_idx >= SSL_PKEY_GOST01;
                     real_idx--) {
                    if (s->cert->pkeys[real_idx].privatekey != NULL) {
                        idx = real_idx;
                        break;
                    }
                }
            }
        } else {
            idx = s->cert->key - s->cert->pkeys;
        }
    }
    if (idx < 0 || idx >= (int)OSSL_NELEM(tls_default_sigalg))
        return NULL;
    if (SSL_USE_SIGALGS(s) || idx != SSL_PKEY_RSA) {
        const SIGALG_LOOKUP *lu = tls1_lookup_sigalg(tls_default_sigalg[idx]);

        if (!tls1_lookup_md(lu, NULL))
            return NULL;
        return lu;
    }
    return &legacy_rsa_sigalg;
}
/* Set peer sigalg based key type */
int tls1_set_peer_legacy_sigalg(SSL *s, const EVP_PKEY *pkey)
{
    size_t idx;
    const SIGALG_LOOKUP *lu;

    if (ssl_cert_lookup_by_pkey(pkey, &idx) == NULL)
        return 0;
    lu = tls1_get_legacy_sigalg(s, idx);
    if (lu == NULL)
        return 0;
    s->s3->tmp.peer_sigalg = lu;
    return 1;
}

size_t tls12_get_psigalgs(SSL *s, int sent, const uint16_t **psigs)
{
    /*
     * If Suite B mode use Suite B sigalgs only, ignore any other
     * preferences.
     */
#ifndef OPENSSL_NO_EC
    switch (tls1_suiteb(s)) {
    case SSL_CERT_FLAG_SUITEB_128_LOS:
        *psigs = suiteb_sigalgs;
        return OSSL_NELEM(suiteb_sigalgs);

    case SSL_CERT_FLAG_SUITEB_128_LOS_ONLY:
        *psigs = suiteb_sigalgs;
        return 1;

    case SSL_CERT_FLAG_SUITEB_192_LOS:
        *psigs = suiteb_sigalgs + 1;
        return 1;
    }
#endif
    /*
     *  We use client_sigalgs (if not NULL) if we're a server
     *  and sending a certificate request or if we're a client and
     *  determining which shared algorithm to use.
     */
    if ((s->server == sent) && s->cert->client_sigalgs != NULL) {
        *psigs = s->cert->client_sigalgs;
        return s->cert->client_sigalgslen;
    } else if (s->cert->conf_sigalgs) {
        *psigs = s->cert->conf_sigalgs;
        return s->cert->conf_sigalgslen;
    } else {
        *psigs = tls12_sigalgs;
        return OSSL_NELEM(tls12_sigalgs);
    }
}

#ifndef OPENSSL_NO_EC
/*
 * Called by servers only. Checks that we have a sig alg that supports the
 * specified EC curve.
 */
int tls_check_sigalg_curve(const SSL *s, int curve)
{
   const uint16_t *sigs;
   size_t siglen, i;

    if (s->cert->conf_sigalgs) {
        sigs = s->cert->conf_sigalgs;
        siglen = s->cert->conf_sigalgslen;
    } else {
        sigs = tls12_sigalgs;
        siglen = OSSL_NELEM(tls12_sigalgs);
    }

    for (i = 0; i < siglen; i++) {
        const SIGALG_LOOKUP *lu = tls1_lookup_sigalg(sigs[i]);

        if (lu == NULL)
            continue;
        if (lu->sig == EVP_PKEY_EC
                && lu->curve != NID_undef
                && curve == lu->curve)
            return 1;
    }

    return 0;
}
#endif

/*
 * Check signature algorithm is consistent with sent supported signature
 * algorithms and if so set relevant digest and signature scheme in
 * s.
 */
int tls12_check_peer_sigalg(SSL *s, uint16_t sig, EVP_PKEY *pkey)
{
    const uint16_t *sent_sigs;
    const EVP_MD *md = NULL;
    char sigalgstr[2];
    size_t sent_sigslen, i, cidx;
    int pkeyid = EVP_PKEY_id(pkey);
    const SIGALG_LOOKUP *lu;

    /* Should never happen */
    if (pkeyid == -1)
        return -1;
    if (SSL_IS_TLS13(s)) {
        /* Disallow DSA for TLS 1.3 */
        if (pkeyid == EVP_PKEY_DSA) {
            SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER, SSL_F_TLS12_CHECK_PEER_SIGALG,
                     SSL_R_WRONG_SIGNATURE_TYPE);
            return 0;
        }
        /* Only allow PSS for TLS 1.3 */
        if (pkeyid == EVP_PKEY_RSA)
            pkeyid = EVP_PKEY_RSA_PSS;
    }
    lu = tls1_lookup_sigalg(sig);
    /*
     * Check sigalgs is known. Disallow SHA1/SHA224 with TLS 1.3. Check key type
     * is consistent with signature: RSA keys can be used for RSA-PSS
     */
    if (lu == NULL
        || (SSL_IS_TLS13(s) && (lu->hash == NID_sha1 || lu->hash == NID_sha224))
        || (pkeyid != lu->sig
        && (lu->sig != EVP_PKEY_RSA_PSS || pkeyid != EVP_PKEY_RSA))) {
        SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER, SSL_F_TLS12_CHECK_PEER_SIGALG,
                 SSL_R_WRONG_SIGNATURE_TYPE);
        return 0;
    }
    /* Check the sigalg is consistent with the key OID */
    if (!ssl_cert_lookup_by_nid(EVP_PKEY_id(pkey), &cidx)
            || lu->sig_idx != (int)cidx) {
        SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER, SSL_F_TLS12_CHECK_PEER_SIGALG,
                 SSL_R_WRONG_SIGNATURE_TYPE);
        return 0;
    }

#ifndef OPENSSL_NO_EC
    if (pkeyid == EVP_PKEY_EC) {

        /* Check point compression is permitted */
        if (!tls1_check_pkey_comp(s, pkey)) {
            SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER,
                     SSL_F_TLS12_CHECK_PEER_SIGALG,
                     SSL_R_ILLEGAL_POINT_COMPRESSION);
            return 0;
        }

        /* For TLS 1.3 or Suite B check curve matches signature algorithm */
        if (SSL_IS_TLS13(s) || tls1_suiteb(s)) {
            EC_KEY *ec = EVP_PKEY_get0_EC_KEY(pkey);
            int curve = EC_GROUP_get_curve_name(EC_KEY_get0_group(ec));

            if (lu->curve != NID_undef && curve != lu->curve) {
                SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER,
                         SSL_F_TLS12_CHECK_PEER_SIGALG, SSL_R_WRONG_CURVE);
                return 0;
            }
        }
        if (!SSL_IS_TLS13(s)) {
            /* Check curve matches extensions */
            if (!tls1_check_group_id(s, tls1_get_group_id(pkey), 1)) {
                SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER,
                         SSL_F_TLS12_CHECK_PEER_SIGALG, SSL_R_WRONG_CURVE);
                return 0;
            }
            if (tls1_suiteb(s)) {
                /* Check sigalg matches a permissible Suite B value */
                if (sig != TLSEXT_SIGALG_ecdsa_secp256r1_sha256
                    && sig != TLSEXT_SIGALG_ecdsa_secp384r1_sha384) {
                    SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE,
                             SSL_F_TLS12_CHECK_PEER_SIGALG,
                             SSL_R_WRONG_SIGNATURE_TYPE);
                    return 0;
                }
            }
        }
    } else if (tls1_suiteb(s)) {
        SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE, SSL_F_TLS12_CHECK_PEER_SIGALG,
                 SSL_R_WRONG_SIGNATURE_TYPE);
        return 0;
    }
#endif

    /* Check signature matches a type we sent */
    sent_sigslen = tls12_get_psigalgs(s, 1, &sent_sigs);
    for (i = 0; i < sent_sigslen; i++, sent_sigs++) {
        if (sig == *sent_sigs)
            break;
    }
    /* Allow fallback to SHA1 if not strict mode */
    if (i == sent_sigslen && (lu->hash != NID_sha1
        || s->cert->cert_flags & SSL_CERT_FLAGS_CHECK_TLS_STRICT)) {
        SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE, SSL_F_TLS12_CHECK_PEER_SIGALG,
                 SSL_R_WRONG_SIGNATURE_TYPE);
        return 0;
    }
    if (!tls1_lookup_md(lu, &md)) {
        SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE, SSL_F_TLS12_CHECK_PEER_SIGALG,
                 SSL_R_UNKNOWN_DIGEST);
        return 0;
    }
    if (md != NULL) {
        /*
         * Make sure security callback allows algorithm. For historical
         * reasons we have to pass the sigalg as a two byte char array.
         */
        sigalgstr[0] = (sig >> 8) & 0xff;
        sigalgstr[1] = sig & 0xff;
        if (!ssl_security(s, SSL_SECOP_SIGALG_CHECK,
                    EVP_MD_size(md) * 4, EVP_MD_type(md),
                    (void *)sigalgstr)) {
            SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE, SSL_F_TLS12_CHECK_PEER_SIGALG,
                     SSL_R_WRONG_SIGNATURE_TYPE);
            return 0;
        }
    }
    /* Store the sigalg the peer uses */
    s->s3->tmp.peer_sigalg = lu;
    return 1;
}

int SSL_get_peer_signature_type_nid(const SSL *s, int *pnid)
{
    if (s->s3->tmp.peer_sigalg == NULL)
        return 0;
    *pnid = s->s3->tmp.peer_sigalg->sig;
    return 1;
}

int SSL_get_signature_type_nid(const SSL *s, int *pnid)
{
    if (s->s3->tmp.sigalg == NULL)
        return 0;
    *pnid = s->s3->tmp.sigalg->sig;
    return 1;
}

/*
 * Set a mask of disabled algorithms: an algorithm is disabled if it isn't
 * supported, doesn't appear in supported signature algorithms, isn't supported
 * by the enabled protocol versions or by the security level.
 *
 * This function should only be used for checking which ciphers are supported
 * by the client.
 *
 * Call ssl_cipher_disabled() to check that it's enabled or not.
 */
int ssl_set_client_disabled(SSL *s)
{
    s->s3->tmp.mask_a = 0;
    s->s3->tmp.mask_k = 0;
    ssl_set_sig_mask(&s->s3->tmp.mask_a, s, SSL_SECOP_SIGALG_MASK);
    if (ssl_get_min_max_version(s, &s->s3->tmp.min_ver,
                                &s->s3->tmp.max_ver, NULL) != 0)
        return 0;
#ifndef OPENSSL_NO_PSK
    /* with PSK there must be client callback set */
    if (!s->psk_client_callback) {
        s->s3->tmp.mask_a |= SSL_aPSK;
        s->s3->tmp.mask_k |= SSL_PSK;
    }
#endif                          /* OPENSSL_NO_PSK */
#ifndef OPENSSL_NO_SRP
    if (!(s->srp_ctx.srp_Mask & SSL_kSRP)) {
        s->s3->tmp.mask_a |= SSL_aSRP;
        s->s3->tmp.mask_k |= SSL_kSRP;
    }
#endif
    return 1;
}

/*
 * ssl_cipher_disabled - check that a cipher is disabled or not
 * @s: SSL connection that you want to use the cipher on
 * @c: cipher to check
 * @op: Security check that you want to do
 * @ecdhe: If set to 1 then TLSv1 ECDHE ciphers are also allowed in SSLv3
 *
 * Returns 1 when it's disabled, 0 when enabled.
 */
int ssl_cipher_disabled(SSL *s, const SSL_CIPHER *c, int op, int ecdhe)
{
    if (c->algorithm_mkey & s->s3->tmp.mask_k
        || c->algorithm_auth & s->s3->tmp.mask_a)
        return 1;
    if (s->s3->tmp.max_ver == 0)
        return 1;
    if (!SSL_IS_DTLS(s)) {
        int min_tls = c->min_tls;

        /*
         * For historical reasons we will allow ECHDE to be selected by a server
         * in SSLv3 if we are a client
         */
        if (min_tls == TLS1_VERSION && ecdhe
                && (c->algorithm_mkey & (SSL_kECDHE | SSL_kECDHEPSK)) != 0)
            min_tls = SSL3_VERSION;

        if ((min_tls > s->s3->tmp.max_ver) || (c->max_tls < s->s3->tmp.min_ver))
            return 1;
    }
    if (SSL_IS_DTLS(s) && (DTLS_VERSION_GT(c->min_dtls, s->s3->tmp.max_ver)
                           || DTLS_VERSION_LT(c->max_dtls, s->s3->tmp.min_ver)))
        return 1;

    return !ssl_security(s, op, c->strength_bits, 0, (void *)c);
}

int tls_use_ticket(SSL *s)
{
    if ((s->options & SSL_OP_NO_TICKET))
        return 0;
    return ssl_security(s, SSL_SECOP_TICKET, 0, 0, NULL);
}

int tls1_set_server_sigalgs(SSL *s)
{
    size_t i;

    /* Clear any shared signature algorithms */
    OPENSSL_free(s->cert->shared_sigalgs);
    s->cert->shared_sigalgs = NULL;
    s->cert->shared_sigalgslen = 0;
    /* Clear certificate validity flags */
    for (i = 0; i < SSL_PKEY_NUM; i++)
        s->s3->tmp.valid_flags[i] = 0;
    /*
     * If peer sent no signature algorithms check to see if we support
     * the default algorithm for each certificate type
     */
    if (s->s3->tmp.peer_cert_sigalgs == NULL
            && s->s3->tmp.peer_sigalgs == NULL) {
        const uint16_t *sent_sigs;
        size_t sent_sigslen = tls12_get_psigalgs(s, 1, &sent_sigs);

        for (i = 0; i < SSL_PKEY_NUM; i++) {
            const SIGALG_LOOKUP *lu = tls1_get_legacy_sigalg(s, i);
            size_t j;

            if (lu == NULL)
                continue;
            /* Check default matches a type we sent */
            for (j = 0; j < sent_sigslen; j++) {
                if (lu->sigalg == sent_sigs[j]) {
                        s->s3->tmp.valid_flags[i] = CERT_PKEY_SIGN;
                        break;
                }
            }
        }
        return 1;
    }

    if (!tls1_process_sigalgs(s)) {
        SSLfatal(s, SSL_AD_INTERNAL_ERROR,
                 SSL_F_TLS1_SET_SERVER_SIGALGS, ERR_R_INTERNAL_ERROR);
        return 0;
    }
    if (s->cert->shared_sigalgs != NULL)
        return 1;

    /* Fatal error if no shared signature algorithms */
    SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE, SSL_F_TLS1_SET_SERVER_SIGALGS,
             SSL_R_NO_SHARED_SIGNATURE_ALGORITHMS);
    return 0;
}

/*-
 * Gets the ticket information supplied by the client if any.
 *
 *   hello: The parsed ClientHello data
 *   ret: (output) on return, if a ticket was decrypted, then this is set to
 *       point to the resulting session.
 */
SSL_TICKET_STATUS tls_get_ticket_from_client(SSL *s, CLIENTHELLO_MSG *hello,
                                             SSL_SESSION **ret)
{
    size_t size;
    RAW_EXTENSION *ticketext;

    *ret = NULL;
    s->ext.ticket_expected = 0;

    /*
     * If tickets disabled or not supported by the protocol version
     * (e.g. TLSv1.3) behave as if no ticket present to permit stateful
     * resumption.
     */
    if (s->version <= SSL3_VERSION || !tls_use_ticket(s))
        return SSL_TICKET_NONE;

    ticketext = &hello->pre_proc_exts[TLSEXT_IDX_session_ticket];
    if (!ticketext->present)
        return SSL_TICKET_NONE;

    size = PACKET_remaining(&ticketext->data);

    return tls_decrypt_ticket(s, PACKET_data(&ticketext->data), size,
                              hello->session_id, hello->session_id_len, ret);
}

/*-
 * tls_decrypt_ticket attempts to decrypt a session ticket.
 *
 * If s->tls_session_secret_cb is set and we're not doing TLSv1.3 then we are
 * expecting a pre-shared key ciphersuite, in which case we have no use for
 * session tickets and one will never be decrypted, nor will
 * s->ext.ticket_expected be set to 1.
 *
 * Side effects:
 *   Sets s->ext.ticket_expected to 1 if the server will have to issue
 *   a new session ticket to the client because the client indicated support
 *   (and s->tls_session_secret_cb is NULL) but the client either doesn't have
 *   a session ticket or we couldn't use the one it gave us, or if
 *   s->ctx->ext.ticket_key_cb asked to renew the client's ticket.
 *   Otherwise, s->ext.ticket_expected is set to 0.
 *
 *   etick: points to the body of the session ticket extension.
 *   eticklen: the length of the session tickets extension.
 *   sess_id: points at the session ID.
 *   sesslen: the length of the session ID.
 *   psess: (output) on return, if a ticket was decrypted, then this is set to
 *       point to the resulting session.
 */
SSL_TICKET_STATUS tls_decrypt_ticket(SSL *s, const unsigned char *etick,
                                     size_t eticklen, const unsigned char *sess_id,
                                     size_t sesslen, SSL_SESSION **psess)
{
    SSL_SESSION *sess = NULL;
    unsigned char *sdec;
    const unsigned char *p;
    int slen, renew_ticket = 0, declen;
    SSL_TICKET_STATUS ret = SSL_TICKET_FATAL_ERR_OTHER;
    size_t mlen;
    unsigned char tick_hmac[EVP_MAX_MD_SIZE];
    HMAC_CTX *hctx = NULL;
    EVP_CIPHER_CTX *ctx = NULL;
    SSL_CTX *tctx = s->session_ctx;

    if (eticklen == 0) {
        /*
         * The client will accept a ticket but doesn't currently have
         * one (TLSv1.2 and below), or treated as a fatal error in TLSv1.3
         */
        ret = SSL_TICKET_EMPTY;
        goto end;
    }
    if (!SSL_IS_TLS13(s) && s->ext.session_secret_cb) {
        /*
         * Indicate that the ticket couldn't be decrypted rather than
         * generating the session from ticket now, trigger
         * abbreviated handshake based on external mechanism to
         * calculate the master secret later.
         */
        ret = SSL_TICKET_NO_DECRYPT;
        goto end;
    }

    /* Need at least keyname + iv */
    if (eticklen < TLSEXT_KEYNAME_LENGTH + EVP_MAX_IV_LENGTH) {
        ret = SSL_TICKET_NO_DECRYPT;
        goto end;
    }

    /* Initialize session ticket encryption and HMAC contexts */
    hctx = HMAC_CTX_new();
    if (hctx == NULL) {
        ret = SSL_TICKET_FATAL_ERR_MALLOC;
        goto end;
    }
    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        ret = SSL_TICKET_FATAL_ERR_MALLOC;
        goto end;
    }
    if (tctx->ext.ticket_key_cb) {
        unsigned char *nctick = (unsigned char *)etick;
        int rv = tctx->ext.ticket_key_cb(s, nctick,
                                         nctick + TLSEXT_KEYNAME_LENGTH,
                                         ctx, hctx, 0);
        if (rv < 0) {
            ret = SSL_TICKET_FATAL_ERR_OTHER;
            goto end;
        }
        if (rv == 0) {
            ret = SSL_TICKET_NO_DECRYPT;
            goto end;
        }
        if (rv == 2)
            renew_ticket = 1;
    } else {
        /* Check key name matches */
        if (memcmp(etick, tctx->ext.tick_key_name,
                   TLSEXT_KEYNAME_LENGTH) != 0) {
            ret = SSL_TICKET_NO_DECRYPT;
            goto end;
        }
        if (HMAC_Init_ex(hctx, tctx->ext.secure->tick_hmac_key,
                         sizeof(tctx->ext.secure->tick_hmac_key),
                         EVP_sha256(), NULL) <= 0
            || EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL,
                                  tctx->ext.secure->tick_aes_key,
                                  etick + TLSEXT_KEYNAME_LENGTH) <= 0) {
            ret = SSL_TICKET_FATAL_ERR_OTHER;
            goto end;
        }
        if (SSL_IS_TLS13(s))
            renew_ticket = 1;
    }
    /*
     * Attempt to process session ticket, first conduct sanity and integrity
     * checks on ticket.
     */
    mlen = HMAC_size(hctx);
    if (mlen == 0) {
        ret = SSL_TICKET_FATAL_ERR_OTHER;
        goto end;
    }

    /* Sanity check ticket length: must exceed keyname + IV + HMAC */
    if (eticklen <=
        TLSEXT_KEYNAME_LENGTH + EVP_CIPHER_CTX_iv_length(ctx) + mlen) {
        ret = SSL_TICKET_NO_DECRYPT;
        goto end;
    }
    eticklen -= mlen;
    /* Check HMAC of encrypted ticket */
    if (HMAC_Update(hctx, etick, eticklen) <= 0
        || HMAC_Final(hctx, tick_hmac, NULL) <= 0) {
        ret = SSL_TICKET_FATAL_ERR_OTHER;
        goto end;
    }

    if (CRYPTO_memcmp(tick_hmac, etick + eticklen, mlen)) {
        ret = SSL_TICKET_NO_DECRYPT;
        goto end;
    }
    /* Attempt to decrypt session data */
    /* Move p after IV to start of encrypted ticket, update length */
    p = etick + TLSEXT_KEYNAME_LENGTH + EVP_CIPHER_CTX_iv_length(ctx);
    eticklen -= TLSEXT_KEYNAME_LENGTH + EVP_CIPHER_CTX_iv_length(ctx);
    sdec = OPENSSL_malloc(eticklen);
    if (sdec == NULL || EVP_DecryptUpdate(ctx, sdec, &slen, p,
                                          (int)eticklen) <= 0) {
        OPENSSL_free(sdec);
        ret = SSL_TICKET_FATAL_ERR_OTHER;
        goto end;
    }
    if (EVP_DecryptFinal(ctx, sdec + slen, &declen) <= 0) {
        OPENSSL_free(sdec);
        ret = SSL_TICKET_NO_DECRYPT;
        goto end;
    }
    slen += declen;
    p = sdec;

    sess = d2i_SSL_SESSION(NULL, &p, slen);
    slen -= p - sdec;
    OPENSSL_free(sdec);
    if (sess) {
        /* Some additional consistency checks */
        if (slen != 0) {
            SSL_SESSION_free(sess);
            sess = NULL;
            ret = SSL_TICKET_NO_DECRYPT;
            goto end;
        }
        /*
         * The session ID, if non-empty, is used by some clients to detect
         * that the ticket has been accepted. So we copy it to the session
         * structure. If it is empty set length to zero as required by
         * standard.
         */
        if (sesslen) {
            memcpy(sess->session_id, sess_id, sesslen);
            sess->session_id_length = sesslen;
        }
        if (renew_ticket)
            ret = SSL_TICKET_SUCCESS_RENEW;
        else
            ret = SSL_TICKET_SUCCESS;
        goto end;
    }
    ERR_clear_error();
    /*
     * For session parse failure, indicate that we need to send a new ticket.
     */
    ret = SSL_TICKET_NO_DECRYPT;

 end:
    EVP_CIPHER_CTX_free(ctx);
    HMAC_CTX_free(hctx);

    /*
     * If set, the decrypt_ticket_cb() is called unless a fatal error was
     * detected above. The callback is responsible for checking |ret| before it
     * performs any action
     */
    if (s->session_ctx->decrypt_ticket_cb != NULL
            && (ret == SSL_TICKET_EMPTY
                || ret == SSL_TICKET_NO_DECRYPT
                || ret == SSL_TICKET_SUCCESS
                || ret == SSL_TICKET_SUCCESS_RENEW)) {
        size_t keyname_len = eticklen;
        int retcb;

        if (keyname_len > TLSEXT_KEYNAME_LENGTH)
            keyname_len = TLSEXT_KEYNAME_LENGTH;
        retcb = s->session_ctx->decrypt_ticket_cb(s, sess, etick, keyname_len,
                                                  ret,
                                                  s->session_ctx->ticket_cb_data);
        switch (retcb) {
        case SSL_TICKET_RETURN_ABORT:
            ret = SSL_TICKET_FATAL_ERR_OTHER;
            break;

        case SSL_TICKET_RETURN_IGNORE:
            ret = SSL_TICKET_NONE;
            SSL_SESSION_free(sess);
            sess = NULL;
            break;

        case SSL_TICKET_RETURN_IGNORE_RENEW:
            if (ret != SSL_TICKET_EMPTY && ret != SSL_TICKET_NO_DECRYPT)
                ret = SSL_TICKET_NO_DECRYPT;
            /* else the value of |ret| will already do the right thing */
            SSL_SESSION_free(sess);
            sess = NULL;
            break;

        case SSL_TICKET_RETURN_USE:
        case SSL_TICKET_RETURN_USE_RENEW:
            if (ret != SSL_TICKET_SUCCESS
                    && ret != SSL_TICKET_SUCCESS_RENEW)
                ret = SSL_TICKET_FATAL_ERR_OTHER;
            else if (retcb == SSL_TICKET_RETURN_USE)
                ret = SSL_TICKET_SUCCESS;
            else
                ret = SSL_TICKET_SUCCESS_RENEW;
            break;

        default:
            ret = SSL_TICKET_FATAL_ERR_OTHER;
        }
    }

    if (s->ext.session_secret_cb == NULL || SSL_IS_TLS13(s)) {
        switch (ret) {
        case SSL_TICKET_NO_DECRYPT:
        case SSL_TICKET_SUCCESS_RENEW:
        case SSL_TICKET_EMPTY:
            s->ext.ticket_expected = 1;
        }
    }

    *psess = sess;

    return ret;
}

/* Check to see if a signature algorithm is allowed */
static int tls12_sigalg_allowed(SSL *s, int op, const SIGALG_LOOKUP *lu)
{
    unsigned char sigalgstr[2];
    int secbits;

    /* See if sigalgs is recognised and if hash is enabled */
    if (!tls1_lookup_md(lu, NULL))
        return 0;
    /* DSA is not allowed in TLS 1.3 */
    if (SSL_IS_TLS13(s) && lu->sig == EVP_PKEY_DSA)
        return 0;
    /* TODO(OpenSSL1.2) fully axe DSA/etc. in ClientHello per TLS 1.3 spec */
    if (!s->server && !SSL_IS_DTLS(s) && s->s3->tmp.min_ver >= TLS1_3_VERSION
        && (lu->sig == EVP_PKEY_DSA || lu->hash_idx == SSL_MD_SHA1_IDX
            || lu->hash_idx == SSL_MD_MD5_IDX
            || lu->hash_idx == SSL_MD_SHA224_IDX))
        return 0;

    /* See if public key algorithm allowed */
    if (ssl_cert_is_disabled(lu->sig_idx))
        return 0;

    if (lu->sig == NID_id_GostR3410_2012_256
            || lu->sig == NID_id_GostR3410_2012_512
            || lu->sig == NID_id_GostR3410_2001) {
        /* We never allow GOST sig algs on the server with TLSv1.3 */
        if (s->server && SSL_IS_TLS13(s))
            return 0;
        if (!s->server
                && s->method->version == TLS_ANY_VERSION
                && s->s3->tmp.max_ver >= TLS1_3_VERSION) {
            int i, num;
            STACK_OF(SSL_CIPHER) *sk;

            /*
             * We're a client that could negotiate TLSv1.3. We only allow GOST
             * sig algs if we could negotiate TLSv1.2 or below and we have GOST
             * ciphersuites enabled.
             */

            if (s->s3->tmp.min_ver >= TLS1_3_VERSION)
                return 0;

            sk = SSL_get_ciphers(s);
            num = sk != NULL ? sk_SSL_CIPHER_num(sk) : 0;
            for (i = 0; i < num; i++) {
                const SSL_CIPHER *c;

                c = sk_SSL_CIPHER_value(sk, i);
                /* Skip disabled ciphers */
                if (ssl_cipher_disabled(s, c, SSL_SECOP_CIPHER_SUPPORTED, 0))
                    continue;

                if ((c->algorithm_mkey & SSL_kGOST) != 0)
                    break;
            }
            if (i == num)
                return 0;
        }
    }

    if (lu->hash == NID_undef)
        return 1;
    /* Security bits: half digest bits */
    secbits = EVP_MD_size(ssl_md(lu->hash_idx)) * 4;
    /* Finally see if security callback allows it */
    sigalgstr[0] = (lu->sigalg >> 8) & 0xff;
    sigalgstr[1] = lu->sigalg & 0xff;
    return ssl_security(s, op, secbits, lu->hash, (void *)sigalgstr);
}

/*
 * Get a mask of disabled public key algorithms based on supported signature
 * algorithms. For example if no signature algorithm supports RSA then RSA is
 * disabled.
 */

void ssl_set_sig_mask(uint32_t *pmask_a, SSL *s, int op)
{
    const uint16_t *sigalgs;
    size_t i, sigalgslen;
    uint32_t disabled_mask = SSL_aRSA | SSL_aDSS | SSL_aECDSA;
    /*
     * Go through all signature algorithms seeing if we support any
     * in disabled_mask.
     */
    sigalgslen = tls12_get_psigalgs(s, 1, &sigalgs);
    for (i = 0; i < sigalgslen; i++, sigalgs++) {
        const SIGALG_LOOKUP *lu = tls1_lookup_sigalg(*sigalgs);
        const SSL_CERT_LOOKUP *clu;

        if (lu == NULL)
            continue;

        clu = ssl_cert_lookup_by_idx(lu->sig_idx);
	if (clu == NULL)
		continue;

        /* If algorithm is disabled see if we can enable it */
        if ((clu->amask & disabled_mask) != 0
                && tls12_sigalg_allowed(s, op, lu))
            disabled_mask &= ~clu->amask;
    }
    *pmask_a |= disabled_mask;
}

int tls12_copy_sigalgs(SSL *s, WPACKET *pkt,
                       const uint16_t *psig, size_t psiglen)
{
    size_t i;
    int rv = 0;

    for (i = 0; i < psiglen; i++, psig++) {
        const SIGALG_LOOKUP *lu = tls1_lookup_sigalg(*psig);

        if (!tls12_sigalg_allowed(s, SSL_SECOP_SIGALG_SUPPORTED, lu))
            continue;
        if (!WPACKET_put_bytes_u16(pkt, *psig))
            return 0;
        /*
         * If TLS 1.3 must have at least one valid TLS 1.3 message
         * signing algorithm: i.e. neither RSA nor SHA1/SHA224
         */
        if (rv == 0 && (!SSL_IS_TLS13(s)
            || (lu->sig != EVP_PKEY_RSA
                && lu->hash != NID_sha1
                && lu->hash != NID_sha224)))
            rv = 1;
    }
    if (rv == 0)
        SSLerr(SSL_F_TLS12_COPY_SIGALGS, SSL_R_NO_SUITABLE_SIGNATURE_ALGORITHM);
    return rv;
}

/* Given preference and allowed sigalgs set shared sigalgs */
static size_t tls12_shared_sigalgs(SSL *s, const SIGALG_LOOKUP **shsig,
                                   const uint16_t *pref, size_t preflen,
                                   const uint16_t *allow, size_t allowlen)
{
    const uint16_t *ptmp, *atmp;
    size_t i, j, nmatch = 0;
    for (i = 0, ptmp = pref; i < preflen; i++, ptmp++) {
        const SIGALG_LOOKUP *lu = tls1_lookup_sigalg(*ptmp);

        /* Skip disabled hashes or signature algorithms */
        if (!tls12_sigalg_allowed(s, SSL_SECOP_SIGALG_SHARED, lu))
            continue;
        for (j = 0, atmp = allow; j < allowlen; j++, atmp++) {
            if (*ptmp == *atmp) {
                nmatch++;
                if (shsig)
                    *shsig++ = lu;
                break;
            }
        }
    }
    return nmatch;
}

/* Set shared signature algorithms for SSL structures */
static int tls1_set_shared_sigalgs(SSL *s)
{
    const uint16_t *pref, *allow, *conf;
    size_t preflen, allowlen, conflen;
    size_t nmatch;
    const SIGALG_LOOKUP **salgs = NULL;
    CERT *c = s->cert;
    unsigned int is_suiteb = tls1_suiteb(s);

    OPENSSL_free(c->shared_sigalgs);
    c->shared_sigalgs = NULL;
    c->shared_sigalgslen = 0;
    /* If client use client signature algorithms if not NULL */
    if (!s->server && c->client_sigalgs && !is_suiteb) {
        conf = c->client_sigalgs;
        conflen = c->client_sigalgslen;
    } else if (c->conf_sigalgs && !is_suiteb) {
        conf = c->conf_sigalgs;
        conflen = c->conf_sigalgslen;
    } else
        conflen = tls12_get_psigalgs(s, 0, &conf);
    if (s->options & SSL_OP_CIPHER_SERVER_PREFERENCE || is_suiteb) {
        pref = conf;
        preflen = conflen;
        allow = s->s3->tmp.peer_sigalgs;
        allowlen = s->s3->tmp.peer_sigalgslen;
    } else {
        allow = conf;
        allowlen = conflen;
        pref = s->s3->tmp.peer_sigalgs;
        preflen = s->s3->tmp.peer_sigalgslen;
    }
    nmatch = tls12_shared_sigalgs(s, NULL, pref, preflen, allow, allowlen);
    if (nmatch) {
        if ((salgs = OPENSSL_malloc(nmatch * sizeof(*salgs))) == NULL) {
            SSLerr(SSL_F_TLS1_SET_SHARED_SIGALGS, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        nmatch = tls12_shared_sigalgs(s, salgs, pref, preflen, allow, allowlen);
    } else {
        salgs = NULL;
    }
    c->shared_sigalgs = salgs;
    c->shared_sigalgslen = nmatch;
    return 1;
}

int tls1_save_u16(PACKET *pkt, uint16_t **pdest, size_t *pdestlen)
{
    unsigned int stmp;
    size_t size, i;
    uint16_t *buf;

    size = PACKET_remaining(pkt);

    /* Invalid data length */
    if (size == 0 || (size & 1) != 0)
        return 0;

    size >>= 1;

    if ((buf = OPENSSL_malloc(size * sizeof(*buf))) == NULL)  {
        SSLerr(SSL_F_TLS1_SAVE_U16, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    for (i = 0; i < size && PACKET_get_net_2(pkt, &stmp); i++)
        buf[i] = stmp;

    if (i != size) {
        OPENSSL_free(buf);
        return 0;
    }

    OPENSSL_free(*pdest);
    *pdest = buf;
    *pdestlen = size;

    return 1;
}

int tls1_save_sigalgs(SSL *s, PACKET *pkt, int cert)
{
    /* Extension ignored for inappropriate versions */
    if (!SSL_USE_SIGALGS(s))
        return 1;
    /* Should never happen */
    if (s->cert == NULL)
        return 0;

    if (cert)
        return tls1_save_u16(pkt, &s->s3->tmp.peer_cert_sigalgs,
                             &s->s3->tmp.peer_cert_sigalgslen);
    else
        return tls1_save_u16(pkt, &s->s3->tmp.peer_sigalgs,
                             &s->s3->tmp.peer_sigalgslen);

}

/* Set preferred digest for each key type */

int tls1_process_sigalgs(SSL *s)
{
    size_t i;
    uint32_t *pvalid = s->s3->tmp.valid_flags;
    CERT *c = s->cert;

    if (!tls1_set_shared_sigalgs(s))
        return 0;

    for (i = 0; i < SSL_PKEY_NUM; i++)
        pvalid[i] = 0;

    for (i = 0; i < c->shared_sigalgslen; i++) {
        const SIGALG_LOOKUP *sigptr = c->shared_sigalgs[i];
        int idx = sigptr->sig_idx;

        /* Ignore PKCS1 based sig algs in TLSv1.3 */
        if (SSL_IS_TLS13(s) && sigptr->sig == EVP_PKEY_RSA)
            continue;
        /* If not disabled indicate we can explicitly sign */
        if (pvalid[idx] == 0 && !ssl_cert_is_disabled(idx))
            pvalid[idx] = CERT_PKEY_EXPLICIT_SIGN | CERT_PKEY_SIGN;
    }
    return 1;
}

int SSL_get_sigalgs(SSL *s, int idx,
                    int *psign, int *phash, int *psignhash,
                    unsigned char *rsig, unsigned char *rhash)
{
    uint16_t *psig = s->s3->tmp.peer_sigalgs;
    size_t numsigalgs = s->s3->tmp.peer_sigalgslen;
    if (psig == NULL || numsigalgs > INT_MAX)
        return 0;
    if (idx >= 0) {
        const SIGALG_LOOKUP *lu;

        if (idx >= (int)numsigalgs)
            return 0;
        psig += idx;
        if (rhash != NULL)
            *rhash = (unsigned char)((*psig >> 8) & 0xff);
        if (rsig != NULL)
            *rsig = (unsigned char)(*psig & 0xff);
        lu = tls1_lookup_sigalg(*psig);
        if (psign != NULL)
            *psign = lu != NULL ? lu->sig : NID_undef;
        if (phash != NULL)
            *phash = lu != NULL ? lu->hash : NID_undef;
        if (psignhash != NULL)
            *psignhash = lu != NULL ? lu->sigandhash : NID_undef;
    }
    return (int)numsigalgs;
}

int SSL_get_shared_sigalgs(SSL *s, int idx,
                           int *psign, int *phash, int *psignhash,
                           unsigned char *rsig, unsigned char *rhash)
{
    const SIGALG_LOOKUP *shsigalgs;
    if (s->cert->shared_sigalgs == NULL
        || idx < 0
        || idx >= (int)s->cert->shared_sigalgslen
        || s->cert->shared_sigalgslen > INT_MAX)
        return 0;
    shsigalgs = s->cert->shared_sigalgs[idx];
    if (phash != NULL)
        *phash = shsigalgs->hash;
    if (psign != NULL)
        *psign = shsigalgs->sig;
    if (psignhash != NULL)
        *psignhash = shsigalgs->sigandhash;
    if (rsig != NULL)
        *rsig = (unsigned char)(shsigalgs->sigalg & 0xff);
    if (rhash != NULL)
        *rhash = (unsigned char)((shsigalgs->sigalg >> 8) & 0xff);
    return (int)s->cert->shared_sigalgslen;
}

/* Maximum possible number of unique entries in sigalgs array */
#define TLS_MAX_SIGALGCNT (OSSL_NELEM(sigalg_lookup_tbl) * 2)

typedef struct {
    size_t sigalgcnt;
    /* TLSEXT_SIGALG_XXX values */
    uint16_t sigalgs[TLS_MAX_SIGALGCNT];
} sig_cb_st;

static void get_sigorhash(int *psig, int *phash, const char *str)
{
    if (strcmp(str, "RSA") == 0) {
        *psig = EVP_PKEY_RSA;
    } else if (strcmp(str, "RSA-PSS") == 0 || strcmp(str, "PSS") == 0) {
        *psig = EVP_PKEY_RSA_PSS;
    } else if (strcmp(str, "DSA") == 0) {
        *psig = EVP_PKEY_DSA;
    } else if (strcmp(str, "ECDSA") == 0) {
        *psig = EVP_PKEY_EC;
    } else {
        *phash = OBJ_sn2nid(str);
        if (*phash == NID_undef)
            *phash = OBJ_ln2nid(str);
    }
}
/* Maximum length of a signature algorithm string component */
#define TLS_MAX_SIGSTRING_LEN   40

static int sig_cb(const char *elem, int len, void *arg)
{
    sig_cb_st *sarg = arg;
    size_t i;
    const SIGALG_LOOKUP *s;
    char etmp[TLS_MAX_SIGSTRING_LEN], *p;
    int sig_alg = NID_undef, hash_alg = NID_undef;
    if (elem == NULL)
        return 0;
    if (sarg->sigalgcnt == TLS_MAX_SIGALGCNT)
        return 0;
    if (len > (int)(sizeof(etmp) - 1))
        return 0;
    memcpy(etmp, elem, len);
    etmp[len] = 0;
    p = strchr(etmp, '+');
    /*
     * We only allow SignatureSchemes listed in the sigalg_lookup_tbl;
     * if there's no '+' in the provided name, look for the new-style combined
     * name.  If not, match both sig+hash to find the needed SIGALG_LOOKUP.
     * Just sig+hash is not unique since TLS 1.3 adds rsa_pss_pss_* and
     * rsa_pss_rsae_* that differ only by public key OID; in such cases
     * we will pick the _rsae_ variant, by virtue of them appearing earlier
     * in the table.
     */
    if (p == NULL) {
        for (i = 0, s = sigalg_lookup_tbl; i < OSSL_NELEM(sigalg_lookup_tbl);
             i++, s++) {
            if (s->name != NULL && strcmp(etmp, s->name) == 0) {
                sarg->sigalgs[sarg->sigalgcnt++] = s->sigalg;
                break;
            }
        }
        if (i == OSSL_NELEM(sigalg_lookup_tbl))
            return 0;
    } else {
        *p = 0;
        p++;
        if (*p == 0)
            return 0;
        get_sigorhash(&sig_alg, &hash_alg, etmp);
        get_sigorhash(&sig_alg, &hash_alg, p);
        if (sig_alg == NID_undef || hash_alg == NID_undef)
            return 0;
        for (i = 0, s = sigalg_lookup_tbl; i < OSSL_NELEM(sigalg_lookup_tbl);
             i++, s++) {
            if (s->hash == hash_alg && s->sig == sig_alg) {
                sarg->sigalgs[sarg->sigalgcnt++] = s->sigalg;
                break;
            }
        }
        if (i == OSSL_NELEM(sigalg_lookup_tbl))
            return 0;
    }

    /* Reject duplicates */
    for (i = 0; i < sarg->sigalgcnt - 1; i++) {
        if (sarg->sigalgs[i] == sarg->sigalgs[sarg->sigalgcnt - 1]) {
            sarg->sigalgcnt--;
            return 0;
        }
    }
    return 1;
}

/*
 * Set supported signature algorithms based on a colon separated list of the
 * form sig+hash e.g. RSA+SHA512:DSA+SHA512
 */
int tls1_set_sigalgs_list(CERT *c, const char *str, int client)
{
    sig_cb_st sig;
    sig.sigalgcnt = 0;
    if (!CONF_parse_list(str, ':', 1, sig_cb, &sig))
        return 0;
    if (c == NULL)
        return 1;
    return tls1_set_raw_sigalgs(c, sig.sigalgs, sig.sigalgcnt, client);
}

int tls1_set_raw_sigalgs(CERT *c, const uint16_t *psigs, size_t salglen,
                     int client)
{
    uint16_t *sigalgs;

    if ((sigalgs = OPENSSL_malloc(salglen * sizeof(*sigalgs))) == NULL) {
        SSLerr(SSL_F_TLS1_SET_RAW_SIGALGS, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    memcpy(sigalgs, psigs, salglen * sizeof(*sigalgs));

    if (client) {
        OPENSSL_free(c->client_sigalgs);
        c->client_sigalgs = sigalgs;
        c->client_sigalgslen = salglen;
    } else {
        OPENSSL_free(c->conf_sigalgs);
        c->conf_sigalgs = sigalgs;
        c->conf_sigalgslen = salglen;
    }

    return 1;
}

int tls1_set_sigalgs(CERT *c, const int *psig_nids, size_t salglen, int client)
{
    uint16_t *sigalgs, *sptr;
    size_t i;

    if (salglen & 1)
        return 0;
    if ((sigalgs = OPENSSL_malloc((salglen / 2) * sizeof(*sigalgs))) == NULL) {
        SSLerr(SSL_F_TLS1_SET_SIGALGS, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    for (i = 0, sptr = sigalgs; i < salglen; i += 2) {
        size_t j;
        const SIGALG_LOOKUP *curr;
        int md_id = *psig_nids++;
        int sig_id = *psig_nids++;

        for (j = 0, curr = sigalg_lookup_tbl; j < OSSL_NELEM(sigalg_lookup_tbl);
             j++, curr++) {
            if (curr->hash == md_id && curr->sig == sig_id) {
                *sptr++ = curr->sigalg;
                break;
            }
        }

        if (j == OSSL_NELEM(sigalg_lookup_tbl))
            goto err;
    }

    if (client) {
        OPENSSL_free(c->client_sigalgs);
        c->client_sigalgs = sigalgs;
        c->client_sigalgslen = salglen / 2;
    } else {
        OPENSSL_free(c->conf_sigalgs);
        c->conf_sigalgs = sigalgs;
        c->conf_sigalgslen = salglen / 2;
    }

    return 1;

 err:
    OPENSSL_free(sigalgs);
    return 0;
}

static int tls1_check_sig_alg(CERT *c, X509 *x, int default_nid)
{
    int sig_nid;
    size_t i;
    if (default_nid == -1)
        return 1;
    sig_nid = X509_get_signature_nid(x);
    if (default_nid)
        return sig_nid == default_nid ? 1 : 0;
    for (i = 0; i < c->shared_sigalgslen; i++)
        if (sig_nid == c->shared_sigalgs[i]->sigandhash)
            return 1;
    return 0;
}

/* Check to see if a certificate issuer name matches list of CA names */
static int ssl_check_ca_name(STACK_OF(X509_NAME) *names, X509 *x)
{
    X509_NAME *nm;
    int i;
    nm = X509_get_issuer_name(x);
    for (i = 0; i < sk_X509_NAME_num(names); i++) {
        if (!X509_NAME_cmp(nm, sk_X509_NAME_value(names, i)))
            return 1;
    }
    return 0;
}

/*
 * Check certificate chain is consistent with TLS extensions and is usable by
 * server. This servers two purposes: it allows users to check chains before
 * passing them to the server and it allows the server to check chains before
 * attempting to use them.
 */

/* Flags which need to be set for a certificate when strict mode not set */

#define CERT_PKEY_VALID_FLAGS \
        (CERT_PKEY_EE_SIGNATURE|CERT_PKEY_EE_PARAM)
/* Strict mode flags */
#define CERT_PKEY_STRICT_FLAGS \
         (CERT_PKEY_VALID_FLAGS|CERT_PKEY_CA_SIGNATURE|CERT_PKEY_CA_PARAM \
         | CERT_PKEY_ISSUER_NAME|CERT_PKEY_CERT_TYPE)

int tls1_check_chain(SSL *s, X509 *x, EVP_PKEY *pk, STACK_OF(X509) *chain,
                     int idx)
{
    int i;
    int rv = 0;
    int check_flags = 0, strict_mode;
    CERT_PKEY *cpk = NULL;
    CERT *c = s->cert;
    uint32_t *pvalid;
    unsigned int suiteb_flags = tls1_suiteb(s);
    /* idx == -1 means checking server chains */
    if (idx != -1) {
        /* idx == -2 means checking client certificate chains */
        if (idx == -2) {
            cpk = c->key;
            idx = (int)(cpk - c->pkeys);
        } else
            cpk = c->pkeys + idx;
        pvalid = s->s3->tmp.valid_flags + idx;
        x = cpk->x509;
        pk = cpk->privatekey;
        chain = cpk->chain;
        strict_mode = c->cert_flags & SSL_CERT_FLAGS_CHECK_TLS_STRICT;
        /* If no cert or key, forget it */
        if (!x || !pk)
            goto end;
    } else {
        size_t certidx;

        if (!x || !pk)
            return 0;

        if (ssl_cert_lookup_by_pkey(pk, &certidx) == NULL)
            return 0;
        idx = certidx;
        pvalid = s->s3->tmp.valid_flags + idx;

        if (c->cert_flags & SSL_CERT_FLAGS_CHECK_TLS_STRICT)
            check_flags = CERT_PKEY_STRICT_FLAGS;
        else
            check_flags = CERT_PKEY_VALID_FLAGS;
        strict_mode = 1;
    }

    if (suiteb_flags) {
        int ok;
        if (check_flags)
            check_flags |= CERT_PKEY_SUITEB;
        ok = X509_chain_check_suiteb(NULL, x, chain, suiteb_flags);
        if (ok == X509_V_OK)
            rv |= CERT_PKEY_SUITEB;
        else if (!check_flags)
            goto end;
    }

    /*
     * Check all signature algorithms are consistent with signature
     * algorithms extension if TLS 1.2 or later and strict mode.
     */
    if (TLS1_get_version(s) >= TLS1_2_VERSION && strict_mode) {
        int default_nid;
        int rsign = 0;
        if (s->s3->tmp.peer_cert_sigalgs != NULL
                || s->s3->tmp.peer_sigalgs != NULL) {
            default_nid = 0;
        /* If no sigalgs extension use defaults from RFC5246 */
        } else {
            switch (idx) {
            case SSL_PKEY_RSA:
                rsign = EVP_PKEY_RSA;
                default_nid = NID_sha1WithRSAEncryption;
                break;

            case SSL_PKEY_DSA_SIGN:
                rsign = EVP_PKEY_DSA;
                default_nid = NID_dsaWithSHA1;
                break;

            case SSL_PKEY_ECC:
                rsign = EVP_PKEY_EC;
                default_nid = NID_ecdsa_with_SHA1;
                break;

            case SSL_PKEY_GOST01:
                rsign = NID_id_GostR3410_2001;
                default_nid = NID_id_GostR3411_94_with_GostR3410_2001;
                break;

            case SSL_PKEY_GOST12_256:
                rsign = NID_id_GostR3410_2012_256;
                default_nid = NID_id_tc26_signwithdigest_gost3410_2012_256;
                break;

            case SSL_PKEY_GOST12_512:
                rsign = NID_id_GostR3410_2012_512;
                default_nid = NID_id_tc26_signwithdigest_gost3410_2012_512;
                break;

            default:
                default_nid = -1;
                break;
            }
        }
        /*
         * If peer sent no signature algorithms extension and we have set
         * preferred signature algorithms check we support sha1.
         */
        if (default_nid > 0 && c->conf_sigalgs) {
            size_t j;
            const uint16_t *p = c->conf_sigalgs;
            for (j = 0; j < c->conf_sigalgslen; j++, p++) {
                const SIGALG_LOOKUP *lu = tls1_lookup_sigalg(*p);

                if (lu != NULL && lu->hash == NID_sha1 && lu->sig == rsign)
                    break;
            }
            if (j == c->conf_sigalgslen) {
                if (check_flags)
                    goto skip_sigs;
                else
                    goto end;
            }
        }
        /* Check signature algorithm of each cert in chain */
        if (!tls1_check_sig_alg(c, x, default_nid)) {
            if (!check_flags)
                goto end;
        } else
            rv |= CERT_PKEY_EE_SIGNATURE;
        rv |= CERT_PKEY_CA_SIGNATURE;
        for (i = 0; i < sk_X509_num(chain); i++) {
            if (!tls1_check_sig_alg(c, sk_X509_value(chain, i), default_nid)) {
                if (check_flags) {
                    rv &= ~CERT_PKEY_CA_SIGNATURE;
                    break;
                } else
                    goto end;
            }
        }
    }
    /* Else not TLS 1.2, so mark EE and CA signing algorithms OK */
    else if (check_flags)
        rv |= CERT_PKEY_EE_SIGNATURE | CERT_PKEY_CA_SIGNATURE;
 skip_sigs:
    /* Check cert parameters are consistent */
    if (tls1_check_cert_param(s, x, 1))
        rv |= CERT_PKEY_EE_PARAM;
    else if (!check_flags)
        goto end;
    if (!s->server)
        rv |= CERT_PKEY_CA_PARAM;
    /* In strict mode check rest of chain too */
    else if (strict_mode) {
        rv |= CERT_PKEY_CA_PARAM;
        for (i = 0; i < sk_X509_num(chain); i++) {
            X509 *ca = sk_X509_value(chain, i);
            if (!tls1_check_cert_param(s, ca, 0)) {
                if (check_flags) {
                    rv &= ~CERT_PKEY_CA_PARAM;
                    break;
                } else
                    goto end;
            }
        }
    }
    if (!s->server && strict_mode) {
        STACK_OF(X509_NAME) *ca_dn;
        int check_type = 0;
        switch (EVP_PKEY_id(pk)) {
        case EVP_PKEY_RSA:
            check_type = TLS_CT_RSA_SIGN;
            break;
        case EVP_PKEY_DSA:
            check_type = TLS_CT_DSS_SIGN;
            break;
        case EVP_PKEY_EC:
            check_type = TLS_CT_ECDSA_SIGN;
            break;
        }
        if (check_type) {
            const uint8_t *ctypes = s->s3->tmp.ctype;
            size_t j;

            for (j = 0; j < s->s3->tmp.ctype_len; j++, ctypes++) {
                if (*ctypes == check_type) {
                    rv |= CERT_PKEY_CERT_TYPE;
                    break;
                }
            }
            if (!(rv & CERT_PKEY_CERT_TYPE) && !check_flags)
                goto end;
        } else {
            rv |= CERT_PKEY_CERT_TYPE;
        }

        ca_dn = s->s3->tmp.peer_ca_names;

        if (!sk_X509_NAME_num(ca_dn))
            rv |= CERT_PKEY_ISSUER_NAME;

        if (!(rv & CERT_PKEY_ISSUER_NAME)) {
            if (ssl_check_ca_name(ca_dn, x))
                rv |= CERT_PKEY_ISSUER_NAME;
        }
        if (!(rv & CERT_PKEY_ISSUER_NAME)) {
            for (i = 0; i < sk_X509_num(chain); i++) {
                X509 *xtmp = sk_X509_value(chain, i);
                if (ssl_check_ca_name(ca_dn, xtmp)) {
                    rv |= CERT_PKEY_ISSUER_NAME;
                    break;
                }
            }
        }
        if (!check_flags && !(rv & CERT_PKEY_ISSUER_NAME))
            goto end;
    } else
        rv |= CERT_PKEY_ISSUER_NAME | CERT_PKEY_CERT_TYPE;

    if (!check_flags || (rv & check_flags) == check_flags)
        rv |= CERT_PKEY_VALID;

 end:

    if (TLS1_get_version(s) >= TLS1_2_VERSION)
        rv |= *pvalid & (CERT_PKEY_EXPLICIT_SIGN | CERT_PKEY_SIGN);
    else
        rv |= CERT_PKEY_SIGN | CERT_PKEY_EXPLICIT_SIGN;

    /*
     * When checking a CERT_PKEY structure all flags are irrelevant if the
     * chain is invalid.
     */
    if (!check_flags) {
        if (rv & CERT_PKEY_VALID) {
            *pvalid = rv;
        } else {
            /* Preserve sign and explicit sign flag, clear rest */
            *pvalid &= CERT_PKEY_EXPLICIT_SIGN | CERT_PKEY_SIGN;
            return 0;
        }
    }
    return rv;
}

/* Set validity of certificates in an SSL structure */
void tls1_set_cert_validity(SSL *s)
{
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_RSA);
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_RSA_PSS_SIGN);
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_DSA_SIGN);
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_ECC);
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_GOST01);
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_GOST12_256);
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_GOST12_512);
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_ED25519);
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_ED448);
}

/* User level utility function to check a chain is suitable */
int SSL_check_chain(SSL *s, X509 *x, EVP_PKEY *pk, STACK_OF(X509) *chain)
{
    return tls1_check_chain(s, x, pk, chain, -1);
}

#ifndef OPENSSL_NO_DH
DH *ssl_get_auto_dh(SSL *s)
{
    int dh_secbits = 80;
    if (s->cert->dh_tmp_auto == 2)
        return DH_get_1024_160();
    if (s->s3->tmp.new_cipher->algorithm_auth & (SSL_aNULL | SSL_aPSK)) {
        if (s->s3->tmp.new_cipher->strength_bits == 256)
            dh_secbits = 128;
        else
            dh_secbits = 80;
    } else {
        if (s->s3->tmp.cert == NULL)
            return NULL;
        dh_secbits = EVP_PKEY_security_bits(s->s3->tmp.cert->privatekey);
    }

    if (dh_secbits >= 128) {
        DH *dhp = DH_new();
        BIGNUM *p, *g;
        if (dhp == NULL)
            return NULL;
        g = BN_new();
        if (g == NULL || !BN_set_word(g, 2)) {
            DH_free(dhp);
            BN_free(g);
            return NULL;
        }
        if (dh_secbits >= 192)
            p = BN_get_rfc3526_prime_8192(NULL);
        else
            p = BN_get_rfc3526_prime_3072(NULL);
        if (p == NULL || !DH_set0_pqg(dhp, p, NULL, g)) {
            DH_free(dhp);
            BN_free(p);
            BN_free(g);
            return NULL;
        }
        return dhp;
    }
    if (dh_secbits >= 112)
        return DH_get_2048_224();
    return DH_get_1024_160();
}
#endif

static int ssl_security_cert_key(SSL *s, SSL_CTX *ctx, X509 *x, int op)
{
    int secbits = -1;
    EVP_PKEY *pkey = X509_get0_pubkey(x);
    if (pkey) {
        /*
         * If no parameters this will return -1 and fail using the default
         * security callback for any non-zero security level. This will
         * reject keys which omit parameters but this only affects DSA and
         * omission of parameters is never (?) done in practice.
         */
        secbits = EVP_PKEY_security_bits(pkey);
    }
    if (s)
        return ssl_security(s, op, secbits, 0, x);
    else
        return ssl_ctx_security(ctx, op, secbits, 0, x);
}

static int ssl_security_cert_sig(SSL *s, SSL_CTX *ctx, X509 *x, int op)
{
    /* Lookup signature algorithm digest */
    int secbits, nid, pknid;
    /* Don't check signature if self signed */
    if ((X509_get_extension_flags(x) & EXFLAG_SS) != 0)
        return 1;
    if (!X509_get_signature_info(x, &nid, &pknid, &secbits, NULL))
        secbits = -1;
    /* If digest NID not defined use signature NID */
    if (nid == NID_undef)
        nid = pknid;
    if (s)
        return ssl_security(s, op, secbits, nid, x);
    else
        return ssl_ctx_security(ctx, op, secbits, nid, x);
}

int ssl_security_cert(SSL *s, SSL_CTX *ctx, X509 *x, int vfy, int is_ee)
{
    if (vfy)
        vfy = SSL_SECOP_PEER;
    if (is_ee) {
        if (!ssl_security_cert_key(s, ctx, x, SSL_SECOP_EE_KEY | vfy))
            return SSL_R_EE_KEY_TOO_SMALL;
    } else {
        if (!ssl_security_cert_key(s, ctx, x, SSL_SECOP_CA_KEY | vfy))
            return SSL_R_CA_KEY_TOO_SMALL;
    }
    if (!ssl_security_cert_sig(s, ctx, x, SSL_SECOP_CA_MD | vfy))
        return SSL_R_CA_MD_TOO_WEAK;
    return 1;
}

/*
 * Check security of a chain, if |sk| includes the end entity certificate then
 * |x| is NULL. If |vfy| is 1 then we are verifying a peer chain and not sending
 * one to the peer. Return values: 1 if ok otherwise error code to use
 */

int ssl_security_cert_chain(SSL *s, STACK_OF(X509) *sk, X509 *x, int vfy)
{
    int rv, start_idx, i;
    if (x == NULL) {
        x = sk_X509_value(sk, 0);
        start_idx = 1;
    } else
        start_idx = 0;

    rv = ssl_security_cert(s, NULL, x, vfy, 1);
    if (rv != 1)
        return rv;

    for (i = start_idx; i < sk_X509_num(sk); i++) {
        x = sk_X509_value(sk, i);
        rv = ssl_security_cert(s, NULL, x, vfy, 0);
        if (rv != 1)
            return rv;
    }
    return 1;
}

/*
 * For TLS 1.2 servers check if we have a certificate which can be used
 * with the signature algorithm "lu" and return index of certificate.
 */

static int tls12_get_cert_sigalg_idx(const SSL *s, const SIGALG_LOOKUP *lu)
{
    int sig_idx = lu->sig_idx;
    const SSL_CERT_LOOKUP *clu = ssl_cert_lookup_by_idx(sig_idx);

    /* If not recognised or not supported by cipher mask it is not suitable */
    if (clu == NULL
            || (clu->amask & s->s3->tmp.new_cipher->algorithm_auth) == 0
            || (clu->nid == EVP_PKEY_RSA_PSS
                && (s->s3->tmp.new_cipher->algorithm_mkey & SSL_kRSA) != 0))
        return -1;

    return s->s3->tmp.valid_flags[sig_idx] & CERT_PKEY_VALID ? sig_idx : -1;
}

/*
 * Returns true if |s| has a usable certificate configured for use
 * with signature scheme |sig|.
 * "Usable" includes a check for presence as well as applying
 * the signature_algorithm_cert restrictions sent by the peer (if any).
 * Returns false if no usable certificate is found.
 */
static int has_usable_cert(SSL *s, const SIGALG_LOOKUP *sig, int idx)
{
    const SIGALG_LOOKUP *lu;
    int mdnid, pknid, default_mdnid;
    int mandatory_md = 0;
    size_t i;

    /* TLS 1.2 callers can override lu->sig_idx, but not TLS 1.3 callers. */
    if (idx == -1)
        idx = sig->sig_idx;
    if (!ssl_has_cert(s, idx))
        return 0;
    /* If the EVP_PKEY reports a mandatory digest, allow nothing else. */
    ERR_set_mark();
    switch (EVP_PKEY_get_default_digest_nid(s->cert->pkeys[idx].privatekey,
                                            &default_mdnid)) {
    case 2:
        mandatory_md = 1;
        break;
    case 1:
        break;
    default: /* If it didn't report a mandatory NID, for whatever reasons,
              * just clear the error and allow all hashes to be used. */
        ERR_pop_to_mark();
    }
    if (s->s3->tmp.peer_cert_sigalgs != NULL) {
        for (i = 0; i < s->s3->tmp.peer_cert_sigalgslen; i++) {
            lu = tls1_lookup_sigalg(s->s3->tmp.peer_cert_sigalgs[i]);
            if (lu == NULL
                || !X509_get_signature_info(s->cert->pkeys[idx].x509, &mdnid,
                                            &pknid, NULL, NULL)
                || (mandatory_md && mdnid != default_mdnid))
                continue;
            /*
             * TODO this does not differentiate between the
             * rsa_pss_pss_* and rsa_pss_rsae_* schemes since we do not
             * have a chain here that lets us look at the key OID in the
             * signing certificate.
             */
            if (mdnid == lu->hash && pknid == lu->sig)
                return 1;
        }
        return 0;
    }
    return !mandatory_md || sig->hash == default_mdnid;
}

/*
 * Choose an appropriate signature algorithm based on available certificates
 * Sets chosen certificate and signature algorithm.
 *
 * For servers if we fail to find a required certificate it is a fatal error,
 * an appropriate error code is set and a TLS alert is sent.
 *
 * For clients fatalerrs is set to 0. If a certificate is not suitable it is not
 * a fatal error: we will either try another certificate or not present one
 * to the server. In this case no error is set.
 */
int tls_choose_sigalg(SSL *s, int fatalerrs)
{
    const SIGALG_LOOKUP *lu = NULL;
    int sig_idx = -1;

    s->s3->tmp.cert = NULL;
    s->s3->tmp.sigalg = NULL;

    if (SSL_IS_TLS13(s)) {
        size_t i;
#ifndef OPENSSL_NO_EC
        int curve = -1;
#endif

        /* Look for a certificate matching shared sigalgs */
        for (i = 0; i < s->cert->shared_sigalgslen; i++) {
            lu = s->cert->shared_sigalgs[i];
            sig_idx = -1;

            /* Skip SHA1, SHA224, DSA and RSA if not PSS */
            if (lu->hash == NID_sha1
                || lu->hash == NID_sha224
                || lu->sig == EVP_PKEY_DSA
                || lu->sig == EVP_PKEY_RSA)
                continue;
            /* Check that we have a cert, and signature_algorithms_cert */
            if (!tls1_lookup_md(lu, NULL) || !has_usable_cert(s, lu, -1))
                continue;
            if (lu->sig == EVP_PKEY_EC) {
#ifndef OPENSSL_NO_EC
                if (curve == -1) {
                    EC_KEY *ec = EVP_PKEY_get0_EC_KEY(s->cert->pkeys[SSL_PKEY_ECC].privatekey);

                    curve = EC_GROUP_get_curve_name(EC_KEY_get0_group(ec));
                }
                if (lu->curve != NID_undef && curve != lu->curve)
                    continue;
#else
                continue;
#endif
            } else if (lu->sig == EVP_PKEY_RSA_PSS) {
                /* validate that key is large enough for the signature algorithm */
                EVP_PKEY *pkey;

                pkey = s->cert->pkeys[lu->sig_idx].privatekey;
                if (!rsa_pss_check_min_key_size(EVP_PKEY_get0(pkey), lu))
                    continue;
            }
            break;
        }
        if (i == s->cert->shared_sigalgslen) {
            if (!fatalerrs)
                return 1;
            SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE, SSL_F_TLS_CHOOSE_SIGALG,
                     SSL_R_NO_SUITABLE_SIGNATURE_ALGORITHM);
            return 0;
        }
    } else {
        /* If ciphersuite doesn't require a cert nothing to do */
        if (!(s->s3->tmp.new_cipher->algorithm_auth & SSL_aCERT))
            return 1;
        if (!s->server && !ssl_has_cert(s, s->cert->key - s->cert->pkeys))
                return 1;

        if (SSL_USE_SIGALGS(s)) {
            size_t i;
            if (s->s3->tmp.peer_sigalgs != NULL) {
#ifndef OPENSSL_NO_EC
                int curve;

                /* For Suite B need to match signature algorithm to curve */
                if (tls1_suiteb(s)) {
                    EC_KEY *ec = EVP_PKEY_get0_EC_KEY(s->cert->pkeys[SSL_PKEY_ECC].privatekey);
                    curve = EC_GROUP_get_curve_name(EC_KEY_get0_group(ec));
                } else {
                    curve = -1;
                }
#endif

                /*
                 * Find highest preference signature algorithm matching
                 * cert type
                 */
                for (i = 0; i < s->cert->shared_sigalgslen; i++) {
                    lu = s->cert->shared_sigalgs[i];

                    if (s->server) {
                        if ((sig_idx = tls12_get_cert_sigalg_idx(s, lu)) == -1)
                            continue;
                    } else {
                        int cc_idx = s->cert->key - s->cert->pkeys;

                        sig_idx = lu->sig_idx;
                        if (cc_idx != sig_idx)
                            continue;
                    }
                    /* Check that we have a cert, and sig_algs_cert */
                    if (!has_usable_cert(s, lu, sig_idx))
                        continue;
                    if (lu->sig == EVP_PKEY_RSA_PSS) {
                        /* validate that key is large enough for the signature algorithm */
                        EVP_PKEY *pkey = s->cert->pkeys[sig_idx].privatekey;

                        if (!rsa_pss_check_min_key_size(EVP_PKEY_get0(pkey), lu))
                            continue;
                    }
#ifndef OPENSSL_NO_EC
                    if (curve == -1 || lu->curve == curve)
#endif
                        break;
                }
                if (i == s->cert->shared_sigalgslen) {
                    if (!fatalerrs)
                        return 1;
                    SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE,
                             SSL_F_TLS_CHOOSE_SIGALG,
                             SSL_R_NO_SUITABLE_SIGNATURE_ALGORITHM);
                    return 0;
                }
            } else {
                /*
                 * If we have no sigalg use defaults
                 */
                const uint16_t *sent_sigs;
                size_t sent_sigslen;

                if ((lu = tls1_get_legacy_sigalg(s, -1)) == NULL) {
                    if (!fatalerrs)
                        return 1;
                    SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_TLS_CHOOSE_SIGALG,
                             ERR_R_INTERNAL_ERROR);
                    return 0;
                }

                /* Check signature matches a type we sent */
                sent_sigslen = tls12_get_psigalgs(s, 1, &sent_sigs);
                for (i = 0; i < sent_sigslen; i++, sent_sigs++) {
                    if (lu->sigalg == *sent_sigs
                            && has_usable_cert(s, lu, lu->sig_idx))
                        break;
                }
                if (i == sent_sigslen) {
                    if (!fatalerrs)
                        return 1;
                    SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER,
                             SSL_F_TLS_CHOOSE_SIGALG,
                             SSL_R_WRONG_SIGNATURE_TYPE);
                    return 0;
                }
            }
        } else {
            if ((lu = tls1_get_legacy_sigalg(s, -1)) == NULL) {
                if (!fatalerrs)
                    return 1;
                SSLfatal(s, SSL_AD_INTERNAL_ERROR, SSL_F_TLS_CHOOSE_SIGALG,
                         ERR_R_INTERNAL_ERROR);
                return 0;
            }
        }
    }
    if (sig_idx == -1)
        sig_idx = lu->sig_idx;
    s->s3->tmp.cert = &s->cert->pkeys[sig_idx];
    s->cert->key = s->s3->tmp.cert;
    s->s3->tmp.sigalg = lu;
    return 1;
}

int SSL_CTX_set_tlsext_max_fragment_length(SSL_CTX *ctx, uint8_t mode)
{
    if (mode != TLSEXT_max_fragment_length_DISABLED
            && !IS_MAX_FRAGMENT_LENGTH_EXT_VALID(mode)) {
        SSLerr(SSL_F_SSL_CTX_SET_TLSEXT_MAX_FRAGMENT_LENGTH,
               SSL_R_SSL3_EXT_INVALID_MAX_FRAGMENT_LENGTH);
        return 0;
    }

    ctx->ext.max_fragment_len_mode = mode;
    return 1;
}

int SSL_set_tlsext_max_fragment_length(SSL *ssl, uint8_t mode)
{
    if (mode != TLSEXT_max_fragment_length_DISABLED
            && !IS_MAX_FRAGMENT_LENGTH_EXT_VALID(mode)) {
        SSLerr(SSL_F_SSL_SET_TLSEXT_MAX_FRAGMENT_LENGTH,
               SSL_R_SSL3_EXT_INVALID_MAX_FRAGMENT_LENGTH);
        return 0;
    }

    ssl->ext.max_fragment_len_mode = mode;
    return 1;
}

uint8_t SSL_SESSION_get_max_fragment_length(const SSL_SESSION *session)
{
    return session->ext.max_fragment_len_mode;
}
