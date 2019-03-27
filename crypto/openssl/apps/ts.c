/*
 * Copyright 2006-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/opensslconf.h>
#ifdef OPENSSL_NO_TS
NON_EMPTY_TRANSLATION_UNIT
#else
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include "apps.h"
# include "progs.h"
# include <openssl/bio.h>
# include <openssl/err.h>
# include <openssl/pem.h>
# include <openssl/rand.h>
# include <openssl/ts.h>
# include <openssl/bn.h>

/* Request nonce length, in bits (must be a multiple of 8). */
# define NONCE_LENGTH            64

/* Name of config entry that defines the OID file. */
# define ENV_OID_FILE            "oid_file"

/* Is |EXACTLY_ONE| of three pointers set? */
# define EXACTLY_ONE(a, b, c) \
        (( a && !b && !c) || \
         ( b && !a && !c) || \
         ( c && !a && !b))

static ASN1_OBJECT *txt2obj(const char *oid);
static CONF *load_config_file(const char *configfile);

/* Query related functions. */
static int query_command(const char *data, const char *digest,
                         const EVP_MD *md, const char *policy, int no_nonce,
                         int cert, const char *in, const char *out, int text);
static TS_REQ *create_query(BIO *data_bio, const char *digest, const EVP_MD *md,
                            const char *policy, int no_nonce, int cert);
static int create_digest(BIO *input, const char *digest,
                         const EVP_MD *md, unsigned char **md_value);
static ASN1_INTEGER *create_nonce(int bits);

/* Reply related functions. */
static int reply_command(CONF *conf, const char *section, const char *engine,
                         const char *queryfile, const char *passin, const char *inkey,
                         const EVP_MD *md, const char *signer, const char *chain,
                         const char *policy, const char *in, int token_in,
                         const char *out, int token_out, int text);
static TS_RESP *read_PKCS7(BIO *in_bio);
static TS_RESP *create_response(CONF *conf, const char *section, const char *engine,
                                const char *queryfile, const char *passin,
                                const char *inkey, const EVP_MD *md, const char *signer,
                                const char *chain, const char *policy);
static ASN1_INTEGER *serial_cb(TS_RESP_CTX *ctx, void *data);
static ASN1_INTEGER *next_serial(const char *serialfile);
static int save_ts_serial(const char *serialfile, ASN1_INTEGER *serial);

/* Verify related functions. */
static int verify_command(const char *data, const char *digest, const char *queryfile,
                          const char *in, int token_in,
                          const char *CApath, const char *CAfile, const char *untrusted,
                          X509_VERIFY_PARAM *vpm);
static TS_VERIFY_CTX *create_verify_ctx(const char *data, const char *digest,
                                        const char *queryfile,
                                        const char *CApath, const char *CAfile,
                                        const char *untrusted,
                                        X509_VERIFY_PARAM *vpm);
static X509_STORE *create_cert_store(const char *CApath, const char *CAfile,
                                     X509_VERIFY_PARAM *vpm);
static int verify_cb(int ok, X509_STORE_CTX *ctx);

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_ENGINE, OPT_CONFIG, OPT_SECTION, OPT_QUERY, OPT_DATA,
    OPT_DIGEST, OPT_TSPOLICY, OPT_NO_NONCE, OPT_CERT,
    OPT_IN, OPT_TOKEN_IN, OPT_OUT, OPT_TOKEN_OUT, OPT_TEXT,
    OPT_REPLY, OPT_QUERYFILE, OPT_PASSIN, OPT_INKEY, OPT_SIGNER,
    OPT_CHAIN, OPT_VERIFY, OPT_CAPATH, OPT_CAFILE, OPT_UNTRUSTED,
    OPT_MD, OPT_V_ENUM, OPT_R_ENUM
} OPTION_CHOICE;

const OPTIONS ts_options[] = {
    {"help", OPT_HELP, '-', "Display this summary"},
    {"config", OPT_CONFIG, '<', "Configuration file"},
    {"section", OPT_SECTION, 's', "Section to use within config file"},
    {"query", OPT_QUERY, '-', "Generate a TS query"},
    {"data", OPT_DATA, '<', "File to hash"},
    {"digest", OPT_DIGEST, 's', "Digest (as a hex string)"},
    OPT_R_OPTIONS,
    {"tspolicy", OPT_TSPOLICY, 's', "Policy OID to use"},
    {"no_nonce", OPT_NO_NONCE, '-', "Do not include a nonce"},
    {"cert", OPT_CERT, '-', "Put cert request into query"},
    {"in", OPT_IN, '<', "Input file"},
    {"token_in", OPT_TOKEN_IN, '-', "Input is a PKCS#7 file"},
    {"out", OPT_OUT, '>', "Output file"},
    {"token_out", OPT_TOKEN_OUT, '-', "Output is a PKCS#7 file"},
    {"text", OPT_TEXT, '-', "Output text (not DER)"},
    {"reply", OPT_REPLY, '-', "Generate a TS reply"},
    {"queryfile", OPT_QUERYFILE, '<', "File containing a TS query"},
    {"passin", OPT_PASSIN, 's', "Input file pass phrase source"},
    {"inkey", OPT_INKEY, 's', "File with private key for reply"},
    {"signer", OPT_SIGNER, 's', "Signer certificate file"},
    {"chain", OPT_CHAIN, '<', "File with signer CA chain"},
    {"verify", OPT_VERIFY, '-', "Verify a TS response"},
    {"CApath", OPT_CAPATH, '/', "Path to trusted CA files"},
    {"CAfile", OPT_CAFILE, '<', "File with trusted CA certs"},
    {"untrusted", OPT_UNTRUSTED, '<', "File with untrusted certs"},
    {"", OPT_MD, '-', "Any supported digest"},
# ifndef OPENSSL_NO_ENGINE
    {"engine", OPT_ENGINE, 's', "Use engine, possibly a hardware device"},
# endif
    {OPT_HELP_STR, 1, '-', "\nOptions specific to 'ts -verify': \n"},
    OPT_V_OPTIONS,
    {OPT_HELP_STR, 1, '-', "\n"},
    {NULL}
};

/*
 * This command is so complex, special help is needed.
 */
static char* opt_helplist[] = {
    "Typical uses:",
    "ts -query [-rand file...] [-config file] [-data file]",
    "          [-digest hexstring] [-tspolicy oid] [-no_nonce] [-cert]",
    "          [-in file] [-out file] [-text]",
    "  or",
    "ts -reply [-config file] [-section tsa_section]",
    "          [-queryfile file] [-passin password]",
    "          [-signer tsa_cert.pem] [-inkey private_key.pem]",
    "          [-chain certs_file.pem] [-tspolicy oid]",
    "          [-in file] [-token_in] [-out file] [-token_out]",
# ifndef OPENSSL_NO_ENGINE
    "          [-text] [-engine id]",
# else
    "          [-text]",
# endif
    "  or",
    "ts -verify -CApath dir -CAfile file.pem -untrusted file.pem",
    "           [-data file] [-digest hexstring]",
    "           [-queryfile file] -in file [-token_in]",
    "           [[options specific to 'ts -verify']]",
    NULL,
};

int ts_main(int argc, char **argv)
{
    CONF *conf = NULL;
    const char *CAfile = NULL, *untrusted = NULL, *prog;
    const char *configfile = default_config_file, *engine = NULL;
    const char *section = NULL;
    char **helpp;
    char *password = NULL;
    char *data = NULL, *digest = NULL, *policy = NULL;
    char *in = NULL, *out = NULL, *queryfile = NULL, *passin = NULL;
    char *inkey = NULL, *signer = NULL, *chain = NULL, *CApath = NULL;
    const EVP_MD *md = NULL;
    OPTION_CHOICE o, mode = OPT_ERR;
    int ret = 1, no_nonce = 0, cert = 0, text = 0;
    int vpmtouched = 0;
    X509_VERIFY_PARAM *vpm = NULL;
    /* Input is ContentInfo instead of TimeStampResp. */
    int token_in = 0;
    /* Output is ContentInfo instead of TimeStampResp. */
    int token_out = 0;

    if ((vpm = X509_VERIFY_PARAM_new()) == NULL)
        goto end;

    prog = opt_init(argc, argv, ts_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
 opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(ts_options);
            for (helpp = opt_helplist; *helpp; ++helpp)
                BIO_printf(bio_err, "%s\n", *helpp);
            ret = 0;
            goto end;
        case OPT_CONFIG:
            configfile = opt_arg();
            break;
        case OPT_SECTION:
            section = opt_arg();
            break;
        case OPT_QUERY:
        case OPT_REPLY:
        case OPT_VERIFY:
            if (mode != OPT_ERR)
                goto opthelp;
            mode = o;
            break;
        case OPT_DATA:
            data = opt_arg();
            break;
        case OPT_DIGEST:
            digest = opt_arg();
            break;
        case OPT_R_CASES:
            if (!opt_rand(o))
                goto end;
            break;
        case OPT_TSPOLICY:
            policy = opt_arg();
            break;
        case OPT_NO_NONCE:
            no_nonce = 1;
            break;
        case OPT_CERT:
            cert = 1;
            break;
        case OPT_IN:
            in = opt_arg();
            break;
        case OPT_TOKEN_IN:
            token_in = 1;
            break;
        case OPT_OUT:
            out = opt_arg();
            break;
        case OPT_TOKEN_OUT:
            token_out = 1;
            break;
        case OPT_TEXT:
            text = 1;
            break;
        case OPT_QUERYFILE:
            queryfile = opt_arg();
            break;
        case OPT_PASSIN:
            passin = opt_arg();
            break;
        case OPT_INKEY:
            inkey = opt_arg();
            break;
        case OPT_SIGNER:
            signer = opt_arg();
            break;
        case OPT_CHAIN:
            chain = opt_arg();
            break;
        case OPT_CAPATH:
            CApath = opt_arg();
            break;
        case OPT_CAFILE:
            CAfile = opt_arg();
            break;
        case OPT_UNTRUSTED:
            untrusted = opt_arg();
            break;
        case OPT_ENGINE:
            engine = opt_arg();
            break;
        case OPT_MD:
            if (!opt_md(opt_unknown(), &md))
                goto opthelp;
            break;
        case OPT_V_CASES:
            if (!opt_verify(o, vpm))
                goto end;
            vpmtouched++;
            break;
        }
    }
    if (mode == OPT_ERR || opt_num_rest() != 0)
        goto opthelp;

    if (mode == OPT_REPLY && passin &&
        !app_passwd(passin, NULL, &password, NULL)) {
        BIO_printf(bio_err, "Error getting password.\n");
        goto end;
    }

    conf = load_config_file(configfile);
    if (configfile != default_config_file && !app_load_modules(conf))
        goto end;

    /* Check parameter consistency and execute the appropriate function. */
    if (mode == OPT_QUERY) {
        if (vpmtouched)
            goto opthelp;
        if ((data != NULL) && (digest != NULL))
            goto opthelp;
        ret = !query_command(data, digest, md, policy, no_nonce, cert,
                             in, out, text);
    } else if (mode == OPT_REPLY) {
        if (vpmtouched)
            goto opthelp;
        if ((in != NULL) && (queryfile != NULL))
            goto opthelp;
        if (in == NULL) {
            if ((conf == NULL) || (token_in != 0))
                goto opthelp;
        }
        ret = !reply_command(conf, section, engine, queryfile,
                             password, inkey, md, signer, chain, policy,
                             in, token_in, out, token_out, text);

    } else if (mode == OPT_VERIFY) {
        if ((in == NULL) || !EXACTLY_ONE(queryfile, data, digest))
            goto opthelp;
        ret = !verify_command(data, digest, queryfile, in, token_in,
                              CApath, CAfile, untrusted,
                              vpmtouched ? vpm : NULL);
    } else {
        goto opthelp;
    }

 end:
    X509_VERIFY_PARAM_free(vpm);
    NCONF_free(conf);
    OPENSSL_free(password);
    return ret;
}

/*
 * Configuration file-related function definitions.
 */

static ASN1_OBJECT *txt2obj(const char *oid)
{
    ASN1_OBJECT *oid_obj = NULL;

    if ((oid_obj = OBJ_txt2obj(oid, 0)) == NULL)
        BIO_printf(bio_err, "cannot convert %s to OID\n", oid);

    return oid_obj;
}

static CONF *load_config_file(const char *configfile)
{
    CONF *conf = app_load_config(configfile);

    if (conf != NULL) {
        const char *p;

        BIO_printf(bio_err, "Using configuration from %s\n", configfile);
        p = NCONF_get_string(conf, NULL, ENV_OID_FILE);
        if (p != NULL) {
            BIO *oid_bio = BIO_new_file(p, "r");
            if (!oid_bio)
                ERR_print_errors(bio_err);
            else {
                OBJ_create_objects(oid_bio);
                BIO_free_all(oid_bio);
            }
        } else
            ERR_clear_error();
        if (!add_oid_section(conf))
            ERR_print_errors(bio_err);
    }
    return conf;
}

/*
 * Query-related method definitions.
 */
static int query_command(const char *data, const char *digest, const EVP_MD *md,
                         const char *policy, int no_nonce,
                         int cert, const char *in, const char *out, int text)
{
    int ret = 0;
    TS_REQ *query = NULL;
    BIO *in_bio = NULL;
    BIO *data_bio = NULL;
    BIO *out_bio = NULL;

    /* Build query object. */
    if (in != NULL) {
        if ((in_bio = bio_open_default(in, 'r', FORMAT_ASN1)) == NULL)
            goto end;
        query = d2i_TS_REQ_bio(in_bio, NULL);
    } else {
        if (digest == NULL
            && (data_bio = bio_open_default(data, 'r', FORMAT_ASN1)) == NULL)
            goto end;
        query = create_query(data_bio, digest, md, policy, no_nonce, cert);
    }
    if (query == NULL)
        goto end;

    if (text) {
        if ((out_bio = bio_open_default(out, 'w', FORMAT_TEXT)) == NULL)
            goto end;
        if (!TS_REQ_print_bio(out_bio, query))
            goto end;
    } else {
        if ((out_bio = bio_open_default(out, 'w', FORMAT_ASN1)) == NULL)
            goto end;
        if (!i2d_TS_REQ_bio(out_bio, query))
            goto end;
    }

    ret = 1;

 end:
    ERR_print_errors(bio_err);
    BIO_free_all(in_bio);
    BIO_free_all(data_bio);
    BIO_free_all(out_bio);
    TS_REQ_free(query);
    return ret;
}

static TS_REQ *create_query(BIO *data_bio, const char *digest, const EVP_MD *md,
                            const char *policy, int no_nonce, int cert)
{
    int ret = 0;
    TS_REQ *ts_req = NULL;
    int len;
    TS_MSG_IMPRINT *msg_imprint = NULL;
    X509_ALGOR *algo = NULL;
    unsigned char *data = NULL;
    ASN1_OBJECT *policy_obj = NULL;
    ASN1_INTEGER *nonce_asn1 = NULL;

    if (md == NULL && (md = EVP_get_digestbyname("sha1")) == NULL)
        goto err;
    if ((ts_req = TS_REQ_new()) == NULL)
        goto err;
    if (!TS_REQ_set_version(ts_req, 1))
        goto err;
    if ((msg_imprint = TS_MSG_IMPRINT_new()) == NULL)
        goto err;
    if ((algo = X509_ALGOR_new()) == NULL)
        goto err;
    if ((algo->algorithm = OBJ_nid2obj(EVP_MD_type(md))) == NULL)
        goto err;
    if ((algo->parameter = ASN1_TYPE_new()) == NULL)
        goto err;
    algo->parameter->type = V_ASN1_NULL;
    if (!TS_MSG_IMPRINT_set_algo(msg_imprint, algo))
        goto err;
    if ((len = create_digest(data_bio, digest, md, &data)) == 0)
        goto err;
    if (!TS_MSG_IMPRINT_set_msg(msg_imprint, data, len))
        goto err;
    if (!TS_REQ_set_msg_imprint(ts_req, msg_imprint))
        goto err;
    if (policy && (policy_obj = txt2obj(policy)) == NULL)
        goto err;
    if (policy_obj && !TS_REQ_set_policy_id(ts_req, policy_obj))
        goto err;

    /* Setting nonce if requested. */
    if (!no_nonce && (nonce_asn1 = create_nonce(NONCE_LENGTH)) == NULL)
        goto err;
    if (nonce_asn1 && !TS_REQ_set_nonce(ts_req, nonce_asn1))
        goto err;
    if (!TS_REQ_set_cert_req(ts_req, cert))
        goto err;

    ret = 1;
 err:
    if (!ret) {
        TS_REQ_free(ts_req);
        ts_req = NULL;
        BIO_printf(bio_err, "could not create query\n");
        ERR_print_errors(bio_err);
    }
    TS_MSG_IMPRINT_free(msg_imprint);
    X509_ALGOR_free(algo);
    OPENSSL_free(data);
    ASN1_OBJECT_free(policy_obj);
    ASN1_INTEGER_free(nonce_asn1);
    return ts_req;
}

static int create_digest(BIO *input, const char *digest, const EVP_MD *md,
                         unsigned char **md_value)
{
    int md_value_len;
    int rv = 0;
    EVP_MD_CTX *md_ctx = NULL;

    md_value_len = EVP_MD_size(md);
    if (md_value_len < 0)
        return 0;

    if (input != NULL) {
        unsigned char buffer[4096];
        int length;

        md_ctx = EVP_MD_CTX_new();
        if (md_ctx == NULL)
            return 0;
        *md_value = app_malloc(md_value_len, "digest buffer");
        if (!EVP_DigestInit(md_ctx, md))
            goto err;
        while ((length = BIO_read(input, buffer, sizeof(buffer))) > 0) {
            if (!EVP_DigestUpdate(md_ctx, buffer, length))
                goto err;
        }
        if (!EVP_DigestFinal(md_ctx, *md_value, NULL))
            goto err;
        md_value_len = EVP_MD_size(md);
    } else {
        long digest_len;
        *md_value = OPENSSL_hexstr2buf(digest, &digest_len);
        if (!*md_value || md_value_len != digest_len) {
            OPENSSL_free(*md_value);
            *md_value = NULL;
            BIO_printf(bio_err, "bad digest, %d bytes "
                       "must be specified\n", md_value_len);
            return 0;
        }
    }
    rv = md_value_len;
 err:
    EVP_MD_CTX_free(md_ctx);
    return rv;
}

static ASN1_INTEGER *create_nonce(int bits)
{
    unsigned char buf[20];
    ASN1_INTEGER *nonce = NULL;
    int len = (bits - 1) / 8 + 1;
    int i;

    if (len > (int)sizeof(buf))
        goto err;
    if (RAND_bytes(buf, len) <= 0)
        goto err;

    /* Find the first non-zero byte and creating ASN1_INTEGER object. */
    for (i = 0; i < len && !buf[i]; ++i)
        continue;
    if ((nonce = ASN1_INTEGER_new()) == NULL)
        goto err;
    OPENSSL_free(nonce->data);
    nonce->length = len - i;
    nonce->data = app_malloc(nonce->length + 1, "nonce buffer");
    memcpy(nonce->data, buf + i, nonce->length);
    return nonce;

 err:
    BIO_printf(bio_err, "could not create nonce\n");
    ASN1_INTEGER_free(nonce);
    return NULL;
}

/*
 * Reply-related method definitions.
 */

static int reply_command(CONF *conf, const char *section, const char *engine,
                         const char *queryfile, const char *passin, const char *inkey,
                         const EVP_MD *md, const char *signer, const char *chain,
                         const char *policy, const char *in, int token_in,
                         const char *out, int token_out, int text)
{
    int ret = 0;
    TS_RESP *response = NULL;
    BIO *in_bio = NULL;
    BIO *query_bio = NULL;
    BIO *inkey_bio = NULL;
    BIO *signer_bio = NULL;
    BIO *out_bio = NULL;

    if (in != NULL) {
        if ((in_bio = BIO_new_file(in, "rb")) == NULL)
            goto end;
        if (token_in) {
            response = read_PKCS7(in_bio);
        } else {
            response = d2i_TS_RESP_bio(in_bio, NULL);
        }
    } else {
        response = create_response(conf, section, engine, queryfile,
                                   passin, inkey, md, signer, chain, policy);
        if (response != NULL)
            BIO_printf(bio_err, "Response has been generated.\n");
        else
            BIO_printf(bio_err, "Response is not generated.\n");
    }
    if (response == NULL)
        goto end;

    /* Write response. */
    if (text) {
        if ((out_bio = bio_open_default(out, 'w', FORMAT_TEXT)) == NULL)
        goto end;
        if (token_out) {
            TS_TST_INFO *tst_info = TS_RESP_get_tst_info(response);
            if (!TS_TST_INFO_print_bio(out_bio, tst_info))
                goto end;
        } else {
            if (!TS_RESP_print_bio(out_bio, response))
                goto end;
        }
    } else {
        if ((out_bio = bio_open_default(out, 'w', FORMAT_ASN1)) == NULL)
            goto end;
        if (token_out) {
            PKCS7 *token = TS_RESP_get_token(response);
            if (!i2d_PKCS7_bio(out_bio, token))
                goto end;
        } else {
            if (!i2d_TS_RESP_bio(out_bio, response))
                goto end;
        }
    }

    ret = 1;

 end:
    ERR_print_errors(bio_err);
    BIO_free_all(in_bio);
    BIO_free_all(query_bio);
    BIO_free_all(inkey_bio);
    BIO_free_all(signer_bio);
    BIO_free_all(out_bio);
    TS_RESP_free(response);
    return ret;
}

/* Reads a PKCS7 token and adds default 'granted' status info to it. */
static TS_RESP *read_PKCS7(BIO *in_bio)
{
    int ret = 0;
    PKCS7 *token = NULL;
    TS_TST_INFO *tst_info = NULL;
    TS_RESP *resp = NULL;
    TS_STATUS_INFO *si = NULL;

    if ((token = d2i_PKCS7_bio(in_bio, NULL)) == NULL)
        goto end;
    if ((tst_info = PKCS7_to_TS_TST_INFO(token)) == NULL)
        goto end;
    if ((resp = TS_RESP_new()) == NULL)
        goto end;
    if ((si = TS_STATUS_INFO_new()) == NULL)
        goto end;
    if (!TS_STATUS_INFO_set_status(si, TS_STATUS_GRANTED))
        goto end;
    if (!TS_RESP_set_status_info(resp, si))
        goto end;
    TS_RESP_set_tst_info(resp, token, tst_info);
    token = NULL;               /* Ownership is lost. */
    tst_info = NULL;            /* Ownership is lost. */
    ret = 1;

 end:
    PKCS7_free(token);
    TS_TST_INFO_free(tst_info);
    if (!ret) {
        TS_RESP_free(resp);
        resp = NULL;
    }
    TS_STATUS_INFO_free(si);
    return resp;
}

static TS_RESP *create_response(CONF *conf, const char *section, const char *engine,
                                const char *queryfile, const char *passin,
                                const char *inkey, const EVP_MD *md, const char *signer,
                                const char *chain, const char *policy)
{
    int ret = 0;
    TS_RESP *response = NULL;
    BIO *query_bio = NULL;
    TS_RESP_CTX *resp_ctx = NULL;

    if ((query_bio = BIO_new_file(queryfile, "rb")) == NULL)
        goto end;
    if ((section = TS_CONF_get_tsa_section(conf, section)) == NULL)
        goto end;
    if ((resp_ctx = TS_RESP_CTX_new()) == NULL)
        goto end;
    if (!TS_CONF_set_serial(conf, section, serial_cb, resp_ctx))
        goto end;
# ifndef OPENSSL_NO_ENGINE
    if (!TS_CONF_set_crypto_device(conf, section, engine))
        goto end;
# endif
    if (!TS_CONF_set_signer_cert(conf, section, signer, resp_ctx))
        goto end;
    if (!TS_CONF_set_certs(conf, section, chain, resp_ctx))
        goto end;
    if (!TS_CONF_set_signer_key(conf, section, inkey, passin, resp_ctx))
        goto end;

    if (md) {
        if (!TS_RESP_CTX_set_signer_digest(resp_ctx, md))
            goto end;
    } else if (!TS_CONF_set_signer_digest(conf, section, NULL, resp_ctx)) {
            goto end;
    }

    if (!TS_CONF_set_ess_cert_id_digest(conf, section, resp_ctx))
        goto end;
    if (!TS_CONF_set_def_policy(conf, section, policy, resp_ctx))
        goto end;
    if (!TS_CONF_set_policies(conf, section, resp_ctx))
        goto end;
    if (!TS_CONF_set_digests(conf, section, resp_ctx))
        goto end;
    if (!TS_CONF_set_accuracy(conf, section, resp_ctx))
        goto end;
    if (!TS_CONF_set_clock_precision_digits(conf, section, resp_ctx))
        goto end;
    if (!TS_CONF_set_ordering(conf, section, resp_ctx))
        goto end;
    if (!TS_CONF_set_tsa_name(conf, section, resp_ctx))
        goto end;
    if (!TS_CONF_set_ess_cert_id_chain(conf, section, resp_ctx))
        goto end;
    if ((response = TS_RESP_create_response(resp_ctx, query_bio)) == NULL)
        goto end;
    ret = 1;

 end:
    if (!ret) {
        TS_RESP_free(response);
        response = NULL;
    }
    TS_RESP_CTX_free(resp_ctx);
    BIO_free_all(query_bio);
    return response;
}

static ASN1_INTEGER *serial_cb(TS_RESP_CTX *ctx, void *data)
{
    const char *serial_file = (const char *)data;
    ASN1_INTEGER *serial = next_serial(serial_file);

    if (serial == NULL) {
        TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
                                    "Error during serial number "
                                    "generation.");
        TS_RESP_CTX_add_failure_info(ctx, TS_INFO_ADD_INFO_NOT_AVAILABLE);
    } else {
        save_ts_serial(serial_file, serial);
    }

    return serial;
}

static ASN1_INTEGER *next_serial(const char *serialfile)
{
    int ret = 0;
    BIO *in = NULL;
    ASN1_INTEGER *serial = NULL;
    BIGNUM *bn = NULL;

    if ((serial = ASN1_INTEGER_new()) == NULL)
        goto err;

    if ((in = BIO_new_file(serialfile, "r")) == NULL) {
        ERR_clear_error();
        BIO_printf(bio_err, "Warning: could not open file %s for "
                   "reading, using serial number: 1\n", serialfile);
        if (!ASN1_INTEGER_set(serial, 1))
            goto err;
    } else {
        char buf[1024];
        if (!a2i_ASN1_INTEGER(in, serial, buf, sizeof(buf))) {
            BIO_printf(bio_err, "unable to load number from %s\n",
                       serialfile);
            goto err;
        }
        if ((bn = ASN1_INTEGER_to_BN(serial, NULL)) == NULL)
            goto err;
        ASN1_INTEGER_free(serial);
        serial = NULL;
        if (!BN_add_word(bn, 1))
            goto err;
        if ((serial = BN_to_ASN1_INTEGER(bn, NULL)) == NULL)
            goto err;
    }
    ret = 1;

 err:
    if (!ret) {
        ASN1_INTEGER_free(serial);
        serial = NULL;
    }
    BIO_free_all(in);
    BN_free(bn);
    return serial;
}

static int save_ts_serial(const char *serialfile, ASN1_INTEGER *serial)
{
    int ret = 0;
    BIO *out = NULL;

    if ((out = BIO_new_file(serialfile, "w")) == NULL)
        goto err;
    if (i2a_ASN1_INTEGER(out, serial) <= 0)
        goto err;
    if (BIO_puts(out, "\n") <= 0)
        goto err;
    ret = 1;
 err:
    if (!ret)
        BIO_printf(bio_err, "could not save serial number to %s\n",
                   serialfile);
    BIO_free_all(out);
    return ret;
}


/*
 * Verify-related method definitions.
 */

static int verify_command(const char *data, const char *digest, const char *queryfile,
                          const char *in, int token_in,
                          const char *CApath, const char *CAfile, const char *untrusted,
                          X509_VERIFY_PARAM *vpm)
{
    BIO *in_bio = NULL;
    PKCS7 *token = NULL;
    TS_RESP *response = NULL;
    TS_VERIFY_CTX *verify_ctx = NULL;
    int ret = 0;

    if ((in_bio = BIO_new_file(in, "rb")) == NULL)
        goto end;
    if (token_in) {
        if ((token = d2i_PKCS7_bio(in_bio, NULL)) == NULL)
            goto end;
    } else {
        if ((response = d2i_TS_RESP_bio(in_bio, NULL)) == NULL)
            goto end;
    }

    if ((verify_ctx = create_verify_ctx(data, digest, queryfile,
                                        CApath, CAfile, untrusted,
                                        vpm)) == NULL)
        goto end;

    ret = token_in
        ? TS_RESP_verify_token(verify_ctx, token)
        : TS_RESP_verify_response(verify_ctx, response);

 end:
    printf("Verification: ");
    if (ret)
        printf("OK\n");
    else {
        printf("FAILED\n");
        ERR_print_errors(bio_err);
    }

    BIO_free_all(in_bio);
    PKCS7_free(token);
    TS_RESP_free(response);
    TS_VERIFY_CTX_free(verify_ctx);
    return ret;
}

static TS_VERIFY_CTX *create_verify_ctx(const char *data, const char *digest,
                                        const char *queryfile,
                                        const char *CApath, const char *CAfile,
                                        const char *untrusted,
                                        X509_VERIFY_PARAM *vpm)
{
    TS_VERIFY_CTX *ctx = NULL;
    BIO *input = NULL;
    TS_REQ *request = NULL;
    int ret = 0;
    int f = 0;

    if (data != NULL || digest != NULL) {
        if ((ctx = TS_VERIFY_CTX_new()) == NULL)
            goto err;
        f = TS_VFY_VERSION | TS_VFY_SIGNER;
        if (data != NULL) {
            BIO *out = NULL;

            f |= TS_VFY_DATA;
            if ((out = BIO_new_file(data, "rb")) == NULL)
                goto err;
            if (TS_VERIFY_CTX_set_data(ctx, out) == NULL) {
                BIO_free_all(out);
                goto err;
            }
        } else if (digest != NULL) {
            long imprint_len;
            unsigned char *hexstr = OPENSSL_hexstr2buf(digest, &imprint_len);
            f |= TS_VFY_IMPRINT;
            if (TS_VERIFY_CTX_set_imprint(ctx, hexstr, imprint_len) == NULL) {
                BIO_printf(bio_err, "invalid digest string\n");
                goto err;
            }
        }

    } else if (queryfile != NULL) {
        if ((input = BIO_new_file(queryfile, "rb")) == NULL)
            goto err;
        if ((request = d2i_TS_REQ_bio(input, NULL)) == NULL)
            goto err;
        if ((ctx = TS_REQ_to_TS_VERIFY_CTX(request, NULL)) == NULL)
            goto err;
    } else {
        return NULL;
    }

    /* Add the signature verification flag and arguments. */
    TS_VERIFY_CTX_add_flags(ctx, f | TS_VFY_SIGNATURE);

    /* Initialising the X509_STORE object. */
    if (TS_VERIFY_CTX_set_store(ctx, create_cert_store(CApath, CAfile, vpm))
            == NULL)
        goto err;

    /* Loading untrusted certificates. */
    if (untrusted
        && TS_VERIFY_CTS_set_certs(ctx, TS_CONF_load_certs(untrusted)) == NULL)
        goto err;
    ret = 1;

 err:
    if (!ret) {
        TS_VERIFY_CTX_free(ctx);
        ctx = NULL;
    }
    BIO_free_all(input);
    TS_REQ_free(request);
    return ctx;
}

static X509_STORE *create_cert_store(const char *CApath, const char *CAfile,
                                     X509_VERIFY_PARAM *vpm)
{
    X509_STORE *cert_ctx = NULL;
    X509_LOOKUP *lookup = NULL;
    int i;

    cert_ctx = X509_STORE_new();
    X509_STORE_set_verify_cb(cert_ctx, verify_cb);
    if (CApath != NULL) {
        lookup = X509_STORE_add_lookup(cert_ctx, X509_LOOKUP_hash_dir());
        if (lookup == NULL) {
            BIO_printf(bio_err, "memory allocation failure\n");
            goto err;
        }
        i = X509_LOOKUP_add_dir(lookup, CApath, X509_FILETYPE_PEM);
        if (!i) {
            BIO_printf(bio_err, "Error loading directory %s\n", CApath);
            goto err;
        }
    }

    if (CAfile != NULL) {
        lookup = X509_STORE_add_lookup(cert_ctx, X509_LOOKUP_file());
        if (lookup == NULL) {
            BIO_printf(bio_err, "memory allocation failure\n");
            goto err;
        }
        i = X509_LOOKUP_load_file(lookup, CAfile, X509_FILETYPE_PEM);
        if (!i) {
            BIO_printf(bio_err, "Error loading file %s\n", CAfile);
            goto err;
        }
    }

    if (vpm != NULL)
        X509_STORE_set1_param(cert_ctx, vpm);

    return cert_ctx;

 err:
    X509_STORE_free(cert_ctx);
    return NULL;
}

static int verify_cb(int ok, X509_STORE_CTX *ctx)
{
    return ok;
}
#endif  /* ndef OPENSSL_NO_TS */
