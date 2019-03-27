/*
 * Copyright 2012-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "ssl_locl.h"
#include <openssl/conf.h>
#include <openssl/objects.h>
#include <openssl/dh.h>
#include "internal/nelem.h"

/*
 * structure holding name tables. This is used for permitted elements in lists
 * such as TLSv1.
 */

typedef struct {
    const char *name;
    int namelen;
    unsigned int name_flags;
    unsigned long option_value;
} ssl_flag_tbl;

/* Switch table: use for single command line switches like no_tls2 */
typedef struct {
    unsigned long option_value;
    unsigned int name_flags;
} ssl_switch_tbl;

/* Sense of name is inverted e.g. "TLSv1" will clear SSL_OP_NO_TLSv1 */
#define SSL_TFLAG_INV   0x1
/* Mask for type of flag referred to */
#define SSL_TFLAG_TYPE_MASK 0xf00
/* Flag is for options */
#define SSL_TFLAG_OPTION    0x000
/* Flag is for cert_flags */
#define SSL_TFLAG_CERT      0x100
/* Flag is for verify mode */
#define SSL_TFLAG_VFY       0x200
/* Option can only be used for clients */
#define SSL_TFLAG_CLIENT SSL_CONF_FLAG_CLIENT
/* Option can only be used for servers */
#define SSL_TFLAG_SERVER SSL_CONF_FLAG_SERVER
#define SSL_TFLAG_BOTH (SSL_TFLAG_CLIENT|SSL_TFLAG_SERVER)

#define SSL_FLAG_TBL(str, flag) \
        {str, (int)(sizeof(str) - 1), SSL_TFLAG_BOTH, flag}
#define SSL_FLAG_TBL_SRV(str, flag) \
        {str, (int)(sizeof(str) - 1), SSL_TFLAG_SERVER, flag}
#define SSL_FLAG_TBL_CLI(str, flag) \
        {str, (int)(sizeof(str) - 1), SSL_TFLAG_CLIENT, flag}
#define SSL_FLAG_TBL_INV(str, flag) \
        {str, (int)(sizeof(str) - 1), SSL_TFLAG_INV|SSL_TFLAG_BOTH, flag}
#define SSL_FLAG_TBL_SRV_INV(str, flag) \
        {str, (int)(sizeof(str) - 1), SSL_TFLAG_INV|SSL_TFLAG_SERVER, flag}
#define SSL_FLAG_TBL_CERT(str, flag) \
        {str, (int)(sizeof(str) - 1), SSL_TFLAG_CERT|SSL_TFLAG_BOTH, flag}

#define SSL_FLAG_VFY_CLI(str, flag) \
        {str, (int)(sizeof(str) - 1), SSL_TFLAG_VFY | SSL_TFLAG_CLIENT, flag}
#define SSL_FLAG_VFY_SRV(str, flag) \
        {str, (int)(sizeof(str) - 1), SSL_TFLAG_VFY | SSL_TFLAG_SERVER, flag}

/*
 * Opaque structure containing SSL configuration context.
 */

struct ssl_conf_ctx_st {
    /*
     * Various flags indicating (among other things) which options we will
     * recognise.
     */
    unsigned int flags;
    /* Prefix and length of commands */
    char *prefix;
    size_t prefixlen;
    /* SSL_CTX or SSL structure to perform operations on */
    SSL_CTX *ctx;
    SSL *ssl;
    /* Pointer to SSL or SSL_CTX options field or NULL if none */
    uint32_t *poptions;
    /* Certificate filenames for each type */
    char *cert_filename[SSL_PKEY_NUM];
    /* Pointer to SSL or SSL_CTX cert_flags or NULL if none */
    uint32_t *pcert_flags;
    /* Pointer to SSL or SSL_CTX verify_mode or NULL if none */
    uint32_t *pvfy_flags;
    /* Pointer to SSL or SSL_CTX min_version field or NULL if none */
    int *min_version;
    /* Pointer to SSL or SSL_CTX max_version field or NULL if none */
    int *max_version;
    /* Current flag table being worked on */
    const ssl_flag_tbl *tbl;
    /* Size of table */
    size_t ntbl;
    /* Client CA names */
    STACK_OF(X509_NAME) *canames;
};

static void ssl_set_option(SSL_CONF_CTX *cctx, unsigned int name_flags,
                           unsigned long option_value, int onoff)
{
    uint32_t *pflags;
    if (cctx->poptions == NULL)
        return;
    if (name_flags & SSL_TFLAG_INV)
        onoff ^= 1;
    switch (name_flags & SSL_TFLAG_TYPE_MASK) {

    case SSL_TFLAG_CERT:
        pflags = cctx->pcert_flags;
        break;

    case SSL_TFLAG_VFY:
        pflags = cctx->pvfy_flags;
        break;

    case SSL_TFLAG_OPTION:
        pflags = cctx->poptions;
        break;

    default:
        return;

    }
    if (onoff)
        *pflags |= option_value;
    else
        *pflags &= ~option_value;
}

static int ssl_match_option(SSL_CONF_CTX *cctx, const ssl_flag_tbl *tbl,
                            const char *name, int namelen, int onoff)
{
    /* If name not relevant for context skip */
    if (!(cctx->flags & tbl->name_flags & SSL_TFLAG_BOTH))
        return 0;
    if (namelen == -1) {
        if (strcmp(tbl->name, name))
            return 0;
    } else if (tbl->namelen != namelen || strncasecmp(tbl->name, name, namelen))
        return 0;
    ssl_set_option(cctx, tbl->name_flags, tbl->option_value, onoff);
    return 1;
}

static int ssl_set_option_list(const char *elem, int len, void *usr)
{
    SSL_CONF_CTX *cctx = usr;
    size_t i;
    const ssl_flag_tbl *tbl;
    int onoff = 1;
    /*
     * len == -1 indicates not being called in list context, just for single
     * command line switches, so don't allow +, -.
     */
    if (elem == NULL)
        return 0;
    if (len != -1) {
        if (*elem == '+') {
            elem++;
            len--;
            onoff = 1;
        } else if (*elem == '-') {
            elem++;
            len--;
            onoff = 0;
        }
    }
    for (i = 0, tbl = cctx->tbl; i < cctx->ntbl; i++, tbl++) {
        if (ssl_match_option(cctx, tbl, elem, len, onoff))
            return 1;
    }
    return 0;
}

/* Set supported signature algorithms */
static int cmd_SignatureAlgorithms(SSL_CONF_CTX *cctx, const char *value)
{
    int rv;
    if (cctx->ssl)
        rv = SSL_set1_sigalgs_list(cctx->ssl, value);
    /* NB: ctx == NULL performs syntax checking only */
    else
        rv = SSL_CTX_set1_sigalgs_list(cctx->ctx, value);
    return rv > 0;
}

/* Set supported client signature algorithms */
static int cmd_ClientSignatureAlgorithms(SSL_CONF_CTX *cctx, const char *value)
{
    int rv;
    if (cctx->ssl)
        rv = SSL_set1_client_sigalgs_list(cctx->ssl, value);
    /* NB: ctx == NULL performs syntax checking only */
    else
        rv = SSL_CTX_set1_client_sigalgs_list(cctx->ctx, value);
    return rv > 0;
}

static int cmd_Groups(SSL_CONF_CTX *cctx, const char *value)
{
    int rv;
    if (cctx->ssl)
        rv = SSL_set1_groups_list(cctx->ssl, value);
    /* NB: ctx == NULL performs syntax checking only */
    else
        rv = SSL_CTX_set1_groups_list(cctx->ctx, value);
    return rv > 0;
}

/* This is the old name for cmd_Groups - retained for backwards compatibility */
static int cmd_Curves(SSL_CONF_CTX *cctx, const char *value)
{
    return cmd_Groups(cctx, value);
}

#ifndef OPENSSL_NO_EC
/* ECDH temporary parameters */
static int cmd_ECDHParameters(SSL_CONF_CTX *cctx, const char *value)
{
    int rv = 1;
    EC_KEY *ecdh;
    int nid;

    /* Ignore values supported by 1.0.2 for the automatic selection */
    if ((cctx->flags & SSL_CONF_FLAG_FILE)
            && (strcasecmp(value, "+automatic") == 0
                || strcasecmp(value, "automatic") == 0))
        return 1;
    if ((cctx->flags & SSL_CONF_FLAG_CMDLINE) &&
        strcmp(value, "auto") == 0)
        return 1;

    nid = EC_curve_nist2nid(value);
    if (nid == NID_undef)
        nid = OBJ_sn2nid(value);
    if (nid == 0)
        return 0;
    ecdh = EC_KEY_new_by_curve_name(nid);
    if (!ecdh)
        return 0;
    if (cctx->ctx)
        rv = SSL_CTX_set_tmp_ecdh(cctx->ctx, ecdh);
    else if (cctx->ssl)
        rv = SSL_set_tmp_ecdh(cctx->ssl, ecdh);
    EC_KEY_free(ecdh);

    return rv > 0;
}
#endif
static int cmd_CipherString(SSL_CONF_CTX *cctx, const char *value)
{
    int rv = 1;

    if (cctx->ctx)
        rv = SSL_CTX_set_cipher_list(cctx->ctx, value);
    if (cctx->ssl)
        rv = SSL_set_cipher_list(cctx->ssl, value);
    return rv > 0;
}

static int cmd_Ciphersuites(SSL_CONF_CTX *cctx, const char *value)
{
    int rv = 1;

    if (cctx->ctx)
        rv = SSL_CTX_set_ciphersuites(cctx->ctx, value);
    if (cctx->ssl)
        rv = SSL_set_ciphersuites(cctx->ssl, value);
    return rv > 0;
}

static int cmd_Protocol(SSL_CONF_CTX *cctx, const char *value)
{
    static const ssl_flag_tbl ssl_protocol_list[] = {
        SSL_FLAG_TBL_INV("ALL", SSL_OP_NO_SSL_MASK),
        SSL_FLAG_TBL_INV("SSLv2", SSL_OP_NO_SSLv2),
        SSL_FLAG_TBL_INV("SSLv3", SSL_OP_NO_SSLv3),
        SSL_FLAG_TBL_INV("TLSv1", SSL_OP_NO_TLSv1),
        SSL_FLAG_TBL_INV("TLSv1.1", SSL_OP_NO_TLSv1_1),
        SSL_FLAG_TBL_INV("TLSv1.2", SSL_OP_NO_TLSv1_2),
        SSL_FLAG_TBL_INV("TLSv1.3", SSL_OP_NO_TLSv1_3),
        SSL_FLAG_TBL_INV("DTLSv1", SSL_OP_NO_DTLSv1),
        SSL_FLAG_TBL_INV("DTLSv1.2", SSL_OP_NO_DTLSv1_2)
    };
    cctx->tbl = ssl_protocol_list;
    cctx->ntbl = OSSL_NELEM(ssl_protocol_list);
    return CONF_parse_list(value, ',', 1, ssl_set_option_list, cctx);
}

/*
 * protocol_from_string - converts a protocol version string to a number
 *
 * Returns -1 on failure or the version on success
 */
static int protocol_from_string(const char *value)
{
    struct protocol_versions {
        const char *name;
        int version;
    };
    static const struct protocol_versions versions[] = {
        {"None", 0},
        {"SSLv3", SSL3_VERSION},
        {"TLSv1", TLS1_VERSION},
        {"TLSv1.1", TLS1_1_VERSION},
        {"TLSv1.2", TLS1_2_VERSION},
        {"TLSv1.3", TLS1_3_VERSION},
        {"DTLSv1", DTLS1_VERSION},
        {"DTLSv1.2", DTLS1_2_VERSION}
    };
    size_t i;
    size_t n = OSSL_NELEM(versions);

    for (i = 0; i < n; i++)
        if (strcmp(versions[i].name, value) == 0)
            return versions[i].version;
    return -1;
}

static int min_max_proto(SSL_CONF_CTX *cctx, const char *value, int *bound)
{
    int method_version;
    int new_version;

    if (cctx->ctx != NULL)
        method_version = cctx->ctx->method->version;
    else if (cctx->ssl != NULL)
        method_version = cctx->ssl->ctx->method->version;
    else
        return 0;
    if ((new_version = protocol_from_string(value)) < 0)
        return 0;
    return ssl_set_version_bound(method_version, new_version, bound);
}

/*
 * cmd_MinProtocol - Set min protocol version
 * @cctx: config structure to save settings in
 * @value: The min protocol version in string form
 *
 * Returns 1 on success and 0 on failure.
 */
static int cmd_MinProtocol(SSL_CONF_CTX *cctx, const char *value)
{
    return min_max_proto(cctx, value, cctx->min_version);
}

/*
 * cmd_MaxProtocol - Set max protocol version
 * @cctx: config structure to save settings in
 * @value: The max protocol version in string form
 *
 * Returns 1 on success and 0 on failure.
 */
static int cmd_MaxProtocol(SSL_CONF_CTX *cctx, const char *value)
{
    return min_max_proto(cctx, value, cctx->max_version);
}

static int cmd_Options(SSL_CONF_CTX *cctx, const char *value)
{
    static const ssl_flag_tbl ssl_option_list[] = {
        SSL_FLAG_TBL_INV("SessionTicket", SSL_OP_NO_TICKET),
        SSL_FLAG_TBL_INV("EmptyFragments",
                         SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS),
        SSL_FLAG_TBL("Bugs", SSL_OP_ALL),
        SSL_FLAG_TBL_INV("Compression", SSL_OP_NO_COMPRESSION),
        SSL_FLAG_TBL_SRV("ServerPreference", SSL_OP_CIPHER_SERVER_PREFERENCE),
        SSL_FLAG_TBL_SRV("NoResumptionOnRenegotiation",
                         SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION),
        SSL_FLAG_TBL_SRV("DHSingle", SSL_OP_SINGLE_DH_USE),
        SSL_FLAG_TBL_SRV("ECDHSingle", SSL_OP_SINGLE_ECDH_USE),
        SSL_FLAG_TBL("UnsafeLegacyRenegotiation",
                     SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION),
        SSL_FLAG_TBL_INV("EncryptThenMac", SSL_OP_NO_ENCRYPT_THEN_MAC),
        SSL_FLAG_TBL("NoRenegotiation", SSL_OP_NO_RENEGOTIATION),
        SSL_FLAG_TBL("AllowNoDHEKEX", SSL_OP_ALLOW_NO_DHE_KEX),
        SSL_FLAG_TBL("PrioritizeChaCha", SSL_OP_PRIORITIZE_CHACHA),
        SSL_FLAG_TBL("MiddleboxCompat", SSL_OP_ENABLE_MIDDLEBOX_COMPAT),
        SSL_FLAG_TBL_INV("AntiReplay", SSL_OP_NO_ANTI_REPLAY)
    };
    if (value == NULL)
        return -3;
    cctx->tbl = ssl_option_list;
    cctx->ntbl = OSSL_NELEM(ssl_option_list);
    return CONF_parse_list(value, ',', 1, ssl_set_option_list, cctx);
}

static int cmd_VerifyMode(SSL_CONF_CTX *cctx, const char *value)
{
    static const ssl_flag_tbl ssl_vfy_list[] = {
        SSL_FLAG_VFY_CLI("Peer", SSL_VERIFY_PEER),
        SSL_FLAG_VFY_SRV("Request", SSL_VERIFY_PEER),
        SSL_FLAG_VFY_SRV("Require",
                         SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT),
        SSL_FLAG_VFY_SRV("Once", SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE),
        SSL_FLAG_VFY_SRV("RequestPostHandshake",
                         SSL_VERIFY_PEER | SSL_VERIFY_POST_HANDSHAKE),
        SSL_FLAG_VFY_SRV("RequirePostHandshake",
                         SSL_VERIFY_PEER | SSL_VERIFY_POST_HANDSHAKE |
                         SSL_VERIFY_FAIL_IF_NO_PEER_CERT),
    };
    if (value == NULL)
        return -3;
    cctx->tbl = ssl_vfy_list;
    cctx->ntbl = OSSL_NELEM(ssl_vfy_list);
    return CONF_parse_list(value, ',', 1, ssl_set_option_list, cctx);
}

static int cmd_Certificate(SSL_CONF_CTX *cctx, const char *value)
{
    int rv = 1;
    CERT *c = NULL;
    if (cctx->ctx) {
        rv = SSL_CTX_use_certificate_chain_file(cctx->ctx, value);
        c = cctx->ctx->cert;
    }
    if (cctx->ssl) {
        rv = SSL_use_certificate_chain_file(cctx->ssl, value);
        c = cctx->ssl->cert;
    }
    if (rv > 0 && c && cctx->flags & SSL_CONF_FLAG_REQUIRE_PRIVATE) {
        char **pfilename = &cctx->cert_filename[c->key - c->pkeys];
        OPENSSL_free(*pfilename);
        *pfilename = OPENSSL_strdup(value);
        if (!*pfilename)
            rv = 0;
    }

    return rv > 0;
}

static int cmd_PrivateKey(SSL_CONF_CTX *cctx, const char *value)
{
    int rv = 1;
    if (!(cctx->flags & SSL_CONF_FLAG_CERTIFICATE))
        return -2;
    if (cctx->ctx)
        rv = SSL_CTX_use_PrivateKey_file(cctx->ctx, value, SSL_FILETYPE_PEM);
    if (cctx->ssl)
        rv = SSL_use_PrivateKey_file(cctx->ssl, value, SSL_FILETYPE_PEM);
    return rv > 0;
}

static int cmd_ServerInfoFile(SSL_CONF_CTX *cctx, const char *value)
{
    int rv = 1;
    if (cctx->ctx)
        rv = SSL_CTX_use_serverinfo_file(cctx->ctx, value);
    return rv > 0;
}

static int do_store(SSL_CONF_CTX *cctx,
                    const char *CAfile, const char *CApath, int verify_store)
{
    CERT *cert;
    X509_STORE **st;
    if (cctx->ctx)
        cert = cctx->ctx->cert;
    else if (cctx->ssl)
        cert = cctx->ssl->cert;
    else
        return 1;
    st = verify_store ? &cert->verify_store : &cert->chain_store;
    if (*st == NULL) {
        *st = X509_STORE_new();
        if (*st == NULL)
            return 0;
    }
    return X509_STORE_load_locations(*st, CAfile, CApath) > 0;
}

static int cmd_ChainCAPath(SSL_CONF_CTX *cctx, const char *value)
{
    return do_store(cctx, NULL, value, 0);
}

static int cmd_ChainCAFile(SSL_CONF_CTX *cctx, const char *value)
{
    return do_store(cctx, value, NULL, 0);
}

static int cmd_VerifyCAPath(SSL_CONF_CTX *cctx, const char *value)
{
    return do_store(cctx, NULL, value, 1);
}

static int cmd_VerifyCAFile(SSL_CONF_CTX *cctx, const char *value)
{
    return do_store(cctx, value, NULL, 1);
}

static int cmd_RequestCAFile(SSL_CONF_CTX *cctx, const char *value)
{
    if (cctx->canames == NULL)
        cctx->canames = sk_X509_NAME_new_null();
    if (cctx->canames == NULL)
        return 0;
    return SSL_add_file_cert_subjects_to_stack(cctx->canames, value);
}

static int cmd_ClientCAFile(SSL_CONF_CTX *cctx, const char *value)
{
    return cmd_RequestCAFile(cctx, value);
}

static int cmd_RequestCAPath(SSL_CONF_CTX *cctx, const char *value)
{
    if (cctx->canames == NULL)
        cctx->canames = sk_X509_NAME_new_null();
    if (cctx->canames == NULL)
        return 0;
    return SSL_add_dir_cert_subjects_to_stack(cctx->canames, value);
}

static int cmd_ClientCAPath(SSL_CONF_CTX *cctx, const char *value)
{
    return cmd_RequestCAPath(cctx, value);
}

#ifndef OPENSSL_NO_DH
static int cmd_DHParameters(SSL_CONF_CTX *cctx, const char *value)
{
    int rv = 0;
    DH *dh = NULL;
    BIO *in = NULL;
    if (cctx->ctx || cctx->ssl) {
        in = BIO_new(BIO_s_file());
        if (in == NULL)
            goto end;
        if (BIO_read_filename(in, value) <= 0)
            goto end;
        dh = PEM_read_bio_DHparams(in, NULL, NULL, NULL);
        if (dh == NULL)
            goto end;
    } else
        return 1;
    if (cctx->ctx)
        rv = SSL_CTX_set_tmp_dh(cctx->ctx, dh);
    if (cctx->ssl)
        rv = SSL_set_tmp_dh(cctx->ssl, dh);
 end:
    DH_free(dh);
    BIO_free(in);
    return rv > 0;
}
#endif

static int cmd_RecordPadding(SSL_CONF_CTX *cctx, const char *value)
{
    int rv = 0;
    int block_size = atoi(value);

    /*
     * All we care about is a non-negative value,
     * the setters check the range
     */
    if (block_size >= 0) {
        if (cctx->ctx)
            rv = SSL_CTX_set_block_padding(cctx->ctx, block_size);
        if (cctx->ssl)
            rv = SSL_set_block_padding(cctx->ssl, block_size);
    }
    return rv;
}


static int cmd_NumTickets(SSL_CONF_CTX *cctx, const char *value)
{
    int rv = 0;
    int num_tickets = atoi(value);

    if (num_tickets >= 0) {
        if (cctx->ctx)
            rv = SSL_CTX_set_num_tickets(cctx->ctx, num_tickets);
        if (cctx->ssl)
            rv = SSL_set_num_tickets(cctx->ssl, num_tickets);
    }
    return rv;
}

typedef struct {
    int (*cmd) (SSL_CONF_CTX *cctx, const char *value);
    const char *str_file;
    const char *str_cmdline;
    unsigned short flags;
    unsigned short value_type;
} ssl_conf_cmd_tbl;

/* Table of supported parameters */

#define SSL_CONF_CMD(name, cmdopt, flags, type) \
        {cmd_##name, #name, cmdopt, flags, type}

#define SSL_CONF_CMD_STRING(name, cmdopt, flags) \
        SSL_CONF_CMD(name, cmdopt, flags, SSL_CONF_TYPE_STRING)

#define SSL_CONF_CMD_SWITCH(name, flags) \
        {0, NULL, name, flags, SSL_CONF_TYPE_NONE}

/* See apps/apps.h if you change this table. */
static const ssl_conf_cmd_tbl ssl_conf_cmds[] = {
    SSL_CONF_CMD_SWITCH("no_ssl3", 0),
    SSL_CONF_CMD_SWITCH("no_tls1", 0),
    SSL_CONF_CMD_SWITCH("no_tls1_1", 0),
    SSL_CONF_CMD_SWITCH("no_tls1_2", 0),
    SSL_CONF_CMD_SWITCH("no_tls1_3", 0),
    SSL_CONF_CMD_SWITCH("bugs", 0),
    SSL_CONF_CMD_SWITCH("no_comp", 0),
    SSL_CONF_CMD_SWITCH("comp", 0),
    SSL_CONF_CMD_SWITCH("ecdh_single", SSL_CONF_FLAG_SERVER),
    SSL_CONF_CMD_SWITCH("no_ticket", 0),
    SSL_CONF_CMD_SWITCH("serverpref", SSL_CONF_FLAG_SERVER),
    SSL_CONF_CMD_SWITCH("legacy_renegotiation", 0),
    SSL_CONF_CMD_SWITCH("legacy_server_connect", SSL_CONF_FLAG_SERVER),
    SSL_CONF_CMD_SWITCH("no_renegotiation", 0),
    SSL_CONF_CMD_SWITCH("no_resumption_on_reneg", SSL_CONF_FLAG_SERVER),
    SSL_CONF_CMD_SWITCH("no_legacy_server_connect", SSL_CONF_FLAG_SERVER),
    SSL_CONF_CMD_SWITCH("allow_no_dhe_kex", 0),
    SSL_CONF_CMD_SWITCH("prioritize_chacha", SSL_CONF_FLAG_SERVER),
    SSL_CONF_CMD_SWITCH("strict", 0),
    SSL_CONF_CMD_SWITCH("no_middlebox", 0),
    SSL_CONF_CMD_SWITCH("anti_replay", SSL_CONF_FLAG_SERVER),
    SSL_CONF_CMD_SWITCH("no_anti_replay", SSL_CONF_FLAG_SERVER),
    SSL_CONF_CMD_STRING(SignatureAlgorithms, "sigalgs", 0),
    SSL_CONF_CMD_STRING(ClientSignatureAlgorithms, "client_sigalgs", 0),
    SSL_CONF_CMD_STRING(Curves, "curves", 0),
    SSL_CONF_CMD_STRING(Groups, "groups", 0),
#ifndef OPENSSL_NO_EC
    SSL_CONF_CMD_STRING(ECDHParameters, "named_curve", SSL_CONF_FLAG_SERVER),
#endif
    SSL_CONF_CMD_STRING(CipherString, "cipher", 0),
    SSL_CONF_CMD_STRING(Ciphersuites, "ciphersuites", 0),
    SSL_CONF_CMD_STRING(Protocol, NULL, 0),
    SSL_CONF_CMD_STRING(MinProtocol, "min_protocol", 0),
    SSL_CONF_CMD_STRING(MaxProtocol, "max_protocol", 0),
    SSL_CONF_CMD_STRING(Options, NULL, 0),
    SSL_CONF_CMD_STRING(VerifyMode, NULL, 0),
    SSL_CONF_CMD(Certificate, "cert", SSL_CONF_FLAG_CERTIFICATE,
                 SSL_CONF_TYPE_FILE),
    SSL_CONF_CMD(PrivateKey, "key", SSL_CONF_FLAG_CERTIFICATE,
                 SSL_CONF_TYPE_FILE),
    SSL_CONF_CMD(ServerInfoFile, NULL,
                 SSL_CONF_FLAG_SERVER | SSL_CONF_FLAG_CERTIFICATE,
                 SSL_CONF_TYPE_FILE),
    SSL_CONF_CMD(ChainCAPath, "chainCApath", SSL_CONF_FLAG_CERTIFICATE,
                 SSL_CONF_TYPE_DIR),
    SSL_CONF_CMD(ChainCAFile, "chainCAfile", SSL_CONF_FLAG_CERTIFICATE,
                 SSL_CONF_TYPE_FILE),
    SSL_CONF_CMD(VerifyCAPath, "verifyCApath", SSL_CONF_FLAG_CERTIFICATE,
                 SSL_CONF_TYPE_DIR),
    SSL_CONF_CMD(VerifyCAFile, "verifyCAfile", SSL_CONF_FLAG_CERTIFICATE,
                 SSL_CONF_TYPE_FILE),
    SSL_CONF_CMD(RequestCAFile, "requestCAFile", SSL_CONF_FLAG_CERTIFICATE,
                 SSL_CONF_TYPE_FILE),
    SSL_CONF_CMD(ClientCAFile, NULL,
                 SSL_CONF_FLAG_SERVER | SSL_CONF_FLAG_CERTIFICATE,
                 SSL_CONF_TYPE_FILE),
    SSL_CONF_CMD(RequestCAPath, NULL, SSL_CONF_FLAG_CERTIFICATE,
                 SSL_CONF_TYPE_DIR),
    SSL_CONF_CMD(ClientCAPath, NULL,
                 SSL_CONF_FLAG_SERVER | SSL_CONF_FLAG_CERTIFICATE,
                 SSL_CONF_TYPE_DIR),
#ifndef OPENSSL_NO_DH
    SSL_CONF_CMD(DHParameters, "dhparam",
                 SSL_CONF_FLAG_SERVER | SSL_CONF_FLAG_CERTIFICATE,
                 SSL_CONF_TYPE_FILE),
#endif
    SSL_CONF_CMD_STRING(RecordPadding, "record_padding", 0),
    SSL_CONF_CMD_STRING(NumTickets, "num_tickets", SSL_CONF_FLAG_SERVER),
};

/* Supported switches: must match order of switches in ssl_conf_cmds */
static const ssl_switch_tbl ssl_cmd_switches[] = {
    {SSL_OP_NO_SSLv3, 0},       /* no_ssl3 */
    {SSL_OP_NO_TLSv1, 0},       /* no_tls1 */
    {SSL_OP_NO_TLSv1_1, 0},     /* no_tls1_1 */
    {SSL_OP_NO_TLSv1_2, 0},     /* no_tls1_2 */
    {SSL_OP_NO_TLSv1_3, 0},     /* no_tls1_3 */
    {SSL_OP_ALL, 0},            /* bugs */
    {SSL_OP_NO_COMPRESSION, 0}, /* no_comp */
    {SSL_OP_NO_COMPRESSION, SSL_TFLAG_INV}, /* comp */
    {SSL_OP_SINGLE_ECDH_USE, 0}, /* ecdh_single */
    {SSL_OP_NO_TICKET, 0},      /* no_ticket */
    {SSL_OP_CIPHER_SERVER_PREFERENCE, 0}, /* serverpref */
    /* legacy_renegotiation */
    {SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION, 0},
    /* legacy_server_connect */
    {SSL_OP_LEGACY_SERVER_CONNECT, 0},
    /* no_renegotiation */
    {SSL_OP_NO_RENEGOTIATION, 0},
    /* no_resumption_on_reneg */
    {SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION, 0},
    /* no_legacy_server_connect */
    {SSL_OP_LEGACY_SERVER_CONNECT, SSL_TFLAG_INV},
    /* allow_no_dhe_kex */
    {SSL_OP_ALLOW_NO_DHE_KEX, 0},
    /* chacha reprioritization */
    {SSL_OP_PRIORITIZE_CHACHA, 0},
    {SSL_CERT_FLAG_TLS_STRICT, SSL_TFLAG_CERT}, /* strict */
    /* no_middlebox */
    {SSL_OP_ENABLE_MIDDLEBOX_COMPAT, SSL_TFLAG_INV},
    /* anti_replay */
    {SSL_OP_NO_ANTI_REPLAY, SSL_TFLAG_INV},
    /* no_anti_replay */
    {SSL_OP_NO_ANTI_REPLAY, 0},
};

static int ssl_conf_cmd_skip_prefix(SSL_CONF_CTX *cctx, const char **pcmd)
{
    if (!pcmd || !*pcmd)
        return 0;
    /* If a prefix is set, check and skip */
    if (cctx->prefix) {
        if (strlen(*pcmd) <= cctx->prefixlen)
            return 0;
        if (cctx->flags & SSL_CONF_FLAG_CMDLINE &&
            strncmp(*pcmd, cctx->prefix, cctx->prefixlen))
            return 0;
        if (cctx->flags & SSL_CONF_FLAG_FILE &&
            strncasecmp(*pcmd, cctx->prefix, cctx->prefixlen))
            return 0;
        *pcmd += cctx->prefixlen;
    } else if (cctx->flags & SSL_CONF_FLAG_CMDLINE) {
        if (**pcmd != '-' || !(*pcmd)[1])
            return 0;
        *pcmd += 1;
    }
    return 1;
}

/* Determine if a command is allowed according to cctx flags */
static int ssl_conf_cmd_allowed(SSL_CONF_CTX *cctx, const ssl_conf_cmd_tbl * t)
{
    unsigned int tfl = t->flags;
    unsigned int cfl = cctx->flags;
    if ((tfl & SSL_CONF_FLAG_SERVER) && !(cfl & SSL_CONF_FLAG_SERVER))
        return 0;
    if ((tfl & SSL_CONF_FLAG_CLIENT) && !(cfl & SSL_CONF_FLAG_CLIENT))
        return 0;
    if ((tfl & SSL_CONF_FLAG_CERTIFICATE)
        && !(cfl & SSL_CONF_FLAG_CERTIFICATE))
        return 0;
    return 1;
}

static const ssl_conf_cmd_tbl *ssl_conf_cmd_lookup(SSL_CONF_CTX *cctx,
                                                   const char *cmd)
{
    const ssl_conf_cmd_tbl *t;
    size_t i;
    if (cmd == NULL)
        return NULL;

    /* Look for matching parameter name in table */
    for (i = 0, t = ssl_conf_cmds; i < OSSL_NELEM(ssl_conf_cmds); i++, t++) {
        if (ssl_conf_cmd_allowed(cctx, t)) {
            if (cctx->flags & SSL_CONF_FLAG_CMDLINE) {
                if (t->str_cmdline && strcmp(t->str_cmdline, cmd) == 0)
                    return t;
            }
            if (cctx->flags & SSL_CONF_FLAG_FILE) {
                if (t->str_file && strcasecmp(t->str_file, cmd) == 0)
                    return t;
            }
        }
    }
    return NULL;
}

static int ctrl_switch_option(SSL_CONF_CTX *cctx, const ssl_conf_cmd_tbl * cmd)
{
    /* Find index of command in table */
    size_t idx = cmd - ssl_conf_cmds;
    const ssl_switch_tbl *scmd;
    /* Sanity check index */
    if (idx >= OSSL_NELEM(ssl_cmd_switches))
        return 0;
    /* Obtain switches entry with same index */
    scmd = ssl_cmd_switches + idx;
    ssl_set_option(cctx, scmd->name_flags, scmd->option_value, 1);
    return 1;
}

int SSL_CONF_cmd(SSL_CONF_CTX *cctx, const char *cmd, const char *value)
{
    const ssl_conf_cmd_tbl *runcmd;
    if (cmd == NULL) {
        SSLerr(SSL_F_SSL_CONF_CMD, SSL_R_INVALID_NULL_CMD_NAME);
        return 0;
    }

    if (!ssl_conf_cmd_skip_prefix(cctx, &cmd))
        return -2;

    runcmd = ssl_conf_cmd_lookup(cctx, cmd);

    if (runcmd) {
        int rv;
        if (runcmd->value_type == SSL_CONF_TYPE_NONE) {
            return ctrl_switch_option(cctx, runcmd);
        }
        if (value == NULL)
            return -3;
        rv = runcmd->cmd(cctx, value);
        if (rv > 0)
            return 2;
        if (rv == -2)
            return -2;
        if (cctx->flags & SSL_CONF_FLAG_SHOW_ERRORS) {
            SSLerr(SSL_F_SSL_CONF_CMD, SSL_R_BAD_VALUE);
            ERR_add_error_data(4, "cmd=", cmd, ", value=", value);
        }
        return 0;
    }

    if (cctx->flags & SSL_CONF_FLAG_SHOW_ERRORS) {
        SSLerr(SSL_F_SSL_CONF_CMD, SSL_R_UNKNOWN_CMD_NAME);
        ERR_add_error_data(2, "cmd=", cmd);
    }

    return -2;
}

int SSL_CONF_cmd_argv(SSL_CONF_CTX *cctx, int *pargc, char ***pargv)
{
    int rv;
    const char *arg = NULL, *argn;
    if (pargc && *pargc == 0)
        return 0;
    if (!pargc || *pargc > 0)
        arg = **pargv;
    if (arg == NULL)
        return 0;
    if (!pargc || *pargc > 1)
        argn = (*pargv)[1];
    else
        argn = NULL;
    cctx->flags &= ~SSL_CONF_FLAG_FILE;
    cctx->flags |= SSL_CONF_FLAG_CMDLINE;
    rv = SSL_CONF_cmd(cctx, arg, argn);
    if (rv > 0) {
        /* Success: update pargc, pargv */
        (*pargv) += rv;
        if (pargc)
            (*pargc) -= rv;
        return rv;
    }
    /* Unknown switch: indicate no arguments processed */
    if (rv == -2)
        return 0;
    /* Some error occurred processing command, return fatal error */
    if (rv == 0)
        return -1;
    return rv;
}

int SSL_CONF_cmd_value_type(SSL_CONF_CTX *cctx, const char *cmd)
{
    if (ssl_conf_cmd_skip_prefix(cctx, &cmd)) {
        const ssl_conf_cmd_tbl *runcmd;
        runcmd = ssl_conf_cmd_lookup(cctx, cmd);
        if (runcmd)
            return runcmd->value_type;
    }
    return SSL_CONF_TYPE_UNKNOWN;
}

SSL_CONF_CTX *SSL_CONF_CTX_new(void)
{
    SSL_CONF_CTX *ret = OPENSSL_zalloc(sizeof(*ret));

    return ret;
}

int SSL_CONF_CTX_finish(SSL_CONF_CTX *cctx)
{
    /* See if any certificates are missing private keys */
    size_t i;
    CERT *c = NULL;
    if (cctx->ctx)
        c = cctx->ctx->cert;
    else if (cctx->ssl)
        c = cctx->ssl->cert;
    if (c && cctx->flags & SSL_CONF_FLAG_REQUIRE_PRIVATE) {
        for (i = 0; i < SSL_PKEY_NUM; i++) {
            const char *p = cctx->cert_filename[i];
            /*
             * If missing private key try to load one from certificate file
             */
            if (p && !c->pkeys[i].privatekey) {
                if (!cmd_PrivateKey(cctx, p))
                    return 0;
            }
        }
    }
    if (cctx->canames) {
        if (cctx->ssl)
            SSL_set0_CA_list(cctx->ssl, cctx->canames);
        else if (cctx->ctx)
            SSL_CTX_set0_CA_list(cctx->ctx, cctx->canames);
        else
            sk_X509_NAME_pop_free(cctx->canames, X509_NAME_free);
        cctx->canames = NULL;
    }
    return 1;
}

void SSL_CONF_CTX_free(SSL_CONF_CTX *cctx)
{
    if (cctx) {
        size_t i;
        for (i = 0; i < SSL_PKEY_NUM; i++)
            OPENSSL_free(cctx->cert_filename[i]);
        OPENSSL_free(cctx->prefix);
        sk_X509_NAME_pop_free(cctx->canames, X509_NAME_free);
        OPENSSL_free(cctx);
    }
}

unsigned int SSL_CONF_CTX_set_flags(SSL_CONF_CTX *cctx, unsigned int flags)
{
    cctx->flags |= flags;
    return cctx->flags;
}

unsigned int SSL_CONF_CTX_clear_flags(SSL_CONF_CTX *cctx, unsigned int flags)
{
    cctx->flags &= ~flags;
    return cctx->flags;
}

int SSL_CONF_CTX_set1_prefix(SSL_CONF_CTX *cctx, const char *pre)
{
    char *tmp = NULL;
    if (pre) {
        tmp = OPENSSL_strdup(pre);
        if (tmp == NULL)
            return 0;
    }
    OPENSSL_free(cctx->prefix);
    cctx->prefix = tmp;
    if (tmp)
        cctx->prefixlen = strlen(tmp);
    else
        cctx->prefixlen = 0;
    return 1;
}

void SSL_CONF_CTX_set_ssl(SSL_CONF_CTX *cctx, SSL *ssl)
{
    cctx->ssl = ssl;
    cctx->ctx = NULL;
    if (ssl) {
        cctx->poptions = &ssl->options;
        cctx->min_version = &ssl->min_proto_version;
        cctx->max_version = &ssl->max_proto_version;
        cctx->pcert_flags = &ssl->cert->cert_flags;
        cctx->pvfy_flags = &ssl->verify_mode;
    } else {
        cctx->poptions = NULL;
        cctx->min_version = NULL;
        cctx->max_version = NULL;
        cctx->pcert_flags = NULL;
        cctx->pvfy_flags = NULL;
    }
}

void SSL_CONF_CTX_set_ssl_ctx(SSL_CONF_CTX *cctx, SSL_CTX *ctx)
{
    cctx->ctx = ctx;
    cctx->ssl = NULL;
    if (ctx) {
        cctx->poptions = &ctx->options;
        cctx->min_version = &ctx->min_proto_version;
        cctx->max_version = &ctx->max_proto_version;
        cctx->pcert_flags = &ctx->cert->cert_flags;
        cctx->pvfy_flags = &ctx->verify_mode;
    } else {
        cctx->poptions = NULL;
        cctx->min_version = NULL;
        cctx->max_version = NULL;
        cctx->pcert_flags = NULL;
        cctx->pvfy_flags = NULL;
    }
}
