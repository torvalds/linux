/*
 * Copyright 2006-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <string.h>

#include <openssl/crypto.h>
#include "internal/cryptlib.h"
#include <openssl/pem.h>
#include <openssl/engine.h>
#include <openssl/ts.h>

/* Macro definitions for the configuration file. */
#define BASE_SECTION                    "tsa"
#define ENV_DEFAULT_TSA                 "default_tsa"
#define ENV_SERIAL                      "serial"
#define ENV_CRYPTO_DEVICE               "crypto_device"
#define ENV_SIGNER_CERT                 "signer_cert"
#define ENV_CERTS                       "certs"
#define ENV_SIGNER_KEY                  "signer_key"
#define ENV_SIGNER_DIGEST               "signer_digest"
#define ENV_DEFAULT_POLICY              "default_policy"
#define ENV_OTHER_POLICIES              "other_policies"
#define ENV_DIGESTS                     "digests"
#define ENV_ACCURACY                    "accuracy"
#define ENV_ORDERING                    "ordering"
#define ENV_TSA_NAME                    "tsa_name"
#define ENV_ESS_CERT_ID_CHAIN           "ess_cert_id_chain"
#define ENV_VALUE_SECS                  "secs"
#define ENV_VALUE_MILLISECS             "millisecs"
#define ENV_VALUE_MICROSECS             "microsecs"
#define ENV_CLOCK_PRECISION_DIGITS      "clock_precision_digits"
#define ENV_VALUE_YES                   "yes"
#define ENV_VALUE_NO                    "no"
#define ENV_ESS_CERT_ID_ALG             "ess_cert_id_alg"

/* Function definitions for certificate and key loading. */

X509 *TS_CONF_load_cert(const char *file)
{
    BIO *cert = NULL;
    X509 *x = NULL;

    if ((cert = BIO_new_file(file, "r")) == NULL)
        goto end;
    x = PEM_read_bio_X509_AUX(cert, NULL, NULL, NULL);
 end:
    if (x == NULL)
        TSerr(TS_F_TS_CONF_LOAD_CERT, TS_R_CANNOT_LOAD_CERT);
    BIO_free(cert);
    return x;
}

STACK_OF(X509) *TS_CONF_load_certs(const char *file)
{
    BIO *certs = NULL;
    STACK_OF(X509) *othercerts = NULL;
    STACK_OF(X509_INFO) *allcerts = NULL;
    int i;

    if ((certs = BIO_new_file(file, "r")) == NULL)
        goto end;
    if ((othercerts = sk_X509_new_null()) == NULL)
        goto end;

    allcerts = PEM_X509_INFO_read_bio(certs, NULL, NULL, NULL);
    for (i = 0; i < sk_X509_INFO_num(allcerts); i++) {
        X509_INFO *xi = sk_X509_INFO_value(allcerts, i);
        if (xi->x509) {
            sk_X509_push(othercerts, xi->x509);
            xi->x509 = NULL;
        }
    }
 end:
    if (othercerts == NULL)
        TSerr(TS_F_TS_CONF_LOAD_CERTS, TS_R_CANNOT_LOAD_CERT);
    sk_X509_INFO_pop_free(allcerts, X509_INFO_free);
    BIO_free(certs);
    return othercerts;
}

EVP_PKEY *TS_CONF_load_key(const char *file, const char *pass)
{
    BIO *key = NULL;
    EVP_PKEY *pkey = NULL;

    if ((key = BIO_new_file(file, "r")) == NULL)
        goto end;
    pkey = PEM_read_bio_PrivateKey(key, NULL, NULL, (char *)pass);
 end:
    if (pkey == NULL)
        TSerr(TS_F_TS_CONF_LOAD_KEY, TS_R_CANNOT_LOAD_KEY);
    BIO_free(key);
    return pkey;
}

/* Function definitions for handling configuration options. */

static void ts_CONF_lookup_fail(const char *name, const char *tag)
{
    TSerr(TS_F_TS_CONF_LOOKUP_FAIL, TS_R_VAR_LOOKUP_FAILURE);
    ERR_add_error_data(3, name, "::", tag);
}

static void ts_CONF_invalid(const char *name, const char *tag)
{
    TSerr(TS_F_TS_CONF_INVALID, TS_R_VAR_BAD_VALUE);
    ERR_add_error_data(3, name, "::", tag);
}

const char *TS_CONF_get_tsa_section(CONF *conf, const char *section)
{
    if (!section) {
        section = NCONF_get_string(conf, BASE_SECTION, ENV_DEFAULT_TSA);
        if (!section)
            ts_CONF_lookup_fail(BASE_SECTION, ENV_DEFAULT_TSA);
    }
    return section;
}

int TS_CONF_set_serial(CONF *conf, const char *section, TS_serial_cb cb,
                       TS_RESP_CTX *ctx)
{
    int ret = 0;
    char *serial = NCONF_get_string(conf, section, ENV_SERIAL);
    if (!serial) {
        ts_CONF_lookup_fail(section, ENV_SERIAL);
        goto err;
    }
    TS_RESP_CTX_set_serial_cb(ctx, cb, serial);

    ret = 1;
 err:
    return ret;
}

#ifndef OPENSSL_NO_ENGINE

int TS_CONF_set_crypto_device(CONF *conf, const char *section,
                              const char *device)
{
    int ret = 0;

    if (device == NULL)
        device = NCONF_get_string(conf, section, ENV_CRYPTO_DEVICE);

    if (device && !TS_CONF_set_default_engine(device)) {
        ts_CONF_invalid(section, ENV_CRYPTO_DEVICE);
        goto err;
    }
    ret = 1;
 err:
    return ret;
}

int TS_CONF_set_default_engine(const char *name)
{
    ENGINE *e = NULL;
    int ret = 0;

    if (strcmp(name, "builtin") == 0)
        return 1;

    if ((e = ENGINE_by_id(name)) == NULL)
        goto err;
    if (strcmp(name, "chil") == 0)
        ENGINE_ctrl(e, ENGINE_CTRL_CHIL_SET_FORKCHECK, 1, 0, 0);
    if (!ENGINE_set_default(e, ENGINE_METHOD_ALL))
        goto err;
    ret = 1;

 err:
    if (!ret) {
        TSerr(TS_F_TS_CONF_SET_DEFAULT_ENGINE, TS_R_COULD_NOT_SET_ENGINE);
        ERR_add_error_data(2, "engine:", name);
    }
    ENGINE_free(e);
    return ret;
}

#endif

int TS_CONF_set_signer_cert(CONF *conf, const char *section,
                            const char *cert, TS_RESP_CTX *ctx)
{
    int ret = 0;
    X509 *cert_obj = NULL;

    if (cert == NULL) {
        cert = NCONF_get_string(conf, section, ENV_SIGNER_CERT);
        if (cert == NULL) {
            ts_CONF_lookup_fail(section, ENV_SIGNER_CERT);
            goto err;
        }
    }
    if ((cert_obj = TS_CONF_load_cert(cert)) == NULL)
        goto err;
    if (!TS_RESP_CTX_set_signer_cert(ctx, cert_obj))
        goto err;

    ret = 1;
 err:
    X509_free(cert_obj);
    return ret;
}

int TS_CONF_set_certs(CONF *conf, const char *section, const char *certs,
                      TS_RESP_CTX *ctx)
{
    int ret = 0;
    STACK_OF(X509) *certs_obj = NULL;

    if (certs == NULL) {
        /* Certificate chain is optional. */
        if ((certs = NCONF_get_string(conf, section, ENV_CERTS)) == NULL)
            goto end;
    }
    if ((certs_obj = TS_CONF_load_certs(certs)) == NULL)
        goto err;
    if (!TS_RESP_CTX_set_certs(ctx, certs_obj))
        goto err;
 end:
    ret = 1;
 err:
    sk_X509_pop_free(certs_obj, X509_free);
    return ret;
}

int TS_CONF_set_signer_key(CONF *conf, const char *section,
                           const char *key, const char *pass,
                           TS_RESP_CTX *ctx)
{
    int ret = 0;
    EVP_PKEY *key_obj = NULL;
    if (!key)
        key = NCONF_get_string(conf, section, ENV_SIGNER_KEY);
    if (!key) {
        ts_CONF_lookup_fail(section, ENV_SIGNER_KEY);
        goto err;
    }
    if ((key_obj = TS_CONF_load_key(key, pass)) == NULL)
        goto err;
    if (!TS_RESP_CTX_set_signer_key(ctx, key_obj))
        goto err;

    ret = 1;
 err:
    EVP_PKEY_free(key_obj);
    return ret;
}

int TS_CONF_set_signer_digest(CONF *conf, const char *section,
                              const char *md, TS_RESP_CTX *ctx)
{
    int ret = 0;
    const EVP_MD *sign_md = NULL;
    if (md == NULL)
        md = NCONF_get_string(conf, section, ENV_SIGNER_DIGEST);
    if (md == NULL) {
        ts_CONF_lookup_fail(section, ENV_SIGNER_DIGEST);
        goto err;
    }
    sign_md = EVP_get_digestbyname(md);
    if (sign_md == NULL) {
        ts_CONF_invalid(section, ENV_SIGNER_DIGEST);
        goto err;
    }
    if (!TS_RESP_CTX_set_signer_digest(ctx, sign_md))
        goto err;

    ret = 1;
 err:
    return ret;
}

int TS_CONF_set_def_policy(CONF *conf, const char *section,
                           const char *policy, TS_RESP_CTX *ctx)
{
    int ret = 0;
    ASN1_OBJECT *policy_obj = NULL;
    if (!policy)
        policy = NCONF_get_string(conf, section, ENV_DEFAULT_POLICY);
    if (!policy) {
        ts_CONF_lookup_fail(section, ENV_DEFAULT_POLICY);
        goto err;
    }
    if ((policy_obj = OBJ_txt2obj(policy, 0)) == NULL) {
        ts_CONF_invalid(section, ENV_DEFAULT_POLICY);
        goto err;
    }
    if (!TS_RESP_CTX_set_def_policy(ctx, policy_obj))
        goto err;

    ret = 1;
 err:
    ASN1_OBJECT_free(policy_obj);
    return ret;
}

int TS_CONF_set_policies(CONF *conf, const char *section, TS_RESP_CTX *ctx)
{
    int ret = 0;
    int i;
    STACK_OF(CONF_VALUE) *list = NULL;
    char *policies = NCONF_get_string(conf, section, ENV_OTHER_POLICIES);

    /* If no other policy is specified, that's fine. */
    if (policies && (list = X509V3_parse_list(policies)) == NULL) {
        ts_CONF_invalid(section, ENV_OTHER_POLICIES);
        goto err;
    }
    for (i = 0; i < sk_CONF_VALUE_num(list); ++i) {
        CONF_VALUE *val = sk_CONF_VALUE_value(list, i);
        const char *extval = val->value ? val->value : val->name;
        ASN1_OBJECT *objtmp;

        if ((objtmp = OBJ_txt2obj(extval, 0)) == NULL) {
            ts_CONF_invalid(section, ENV_OTHER_POLICIES);
            goto err;
        }
        if (!TS_RESP_CTX_add_policy(ctx, objtmp))
            goto err;
        ASN1_OBJECT_free(objtmp);
    }

    ret = 1;
 err:
    sk_CONF_VALUE_pop_free(list, X509V3_conf_free);
    return ret;
}

int TS_CONF_set_digests(CONF *conf, const char *section, TS_RESP_CTX *ctx)
{
    int ret = 0;
    int i;
    STACK_OF(CONF_VALUE) *list = NULL;
    char *digests = NCONF_get_string(conf, section, ENV_DIGESTS);

    if (digests == NULL) {
        ts_CONF_lookup_fail(section, ENV_DIGESTS);
        goto err;
    }
    if ((list = X509V3_parse_list(digests)) == NULL) {
        ts_CONF_invalid(section, ENV_DIGESTS);
        goto err;
    }
    if (sk_CONF_VALUE_num(list) == 0) {
        ts_CONF_invalid(section, ENV_DIGESTS);
        goto err;
    }
    for (i = 0; i < sk_CONF_VALUE_num(list); ++i) {
        CONF_VALUE *val = sk_CONF_VALUE_value(list, i);
        const char *extval = val->value ? val->value : val->name;
        const EVP_MD *md;

        if ((md = EVP_get_digestbyname(extval)) == NULL) {
            ts_CONF_invalid(section, ENV_DIGESTS);
            goto err;
        }
        if (!TS_RESP_CTX_add_md(ctx, md))
            goto err;
    }

    ret = 1;
 err:
    sk_CONF_VALUE_pop_free(list, X509V3_conf_free);
    return ret;
}

int TS_CONF_set_accuracy(CONF *conf, const char *section, TS_RESP_CTX *ctx)
{
    int ret = 0;
    int i;
    int secs = 0, millis = 0, micros = 0;
    STACK_OF(CONF_VALUE) *list = NULL;
    char *accuracy = NCONF_get_string(conf, section, ENV_ACCURACY);

    if (accuracy && (list = X509V3_parse_list(accuracy)) == NULL) {
        ts_CONF_invalid(section, ENV_ACCURACY);
        goto err;
    }
    for (i = 0; i < sk_CONF_VALUE_num(list); ++i) {
        CONF_VALUE *val = sk_CONF_VALUE_value(list, i);
        if (strcmp(val->name, ENV_VALUE_SECS) == 0) {
            if (val->value)
                secs = atoi(val->value);
        } else if (strcmp(val->name, ENV_VALUE_MILLISECS) == 0) {
            if (val->value)
                millis = atoi(val->value);
        } else if (strcmp(val->name, ENV_VALUE_MICROSECS) == 0) {
            if (val->value)
                micros = atoi(val->value);
        } else {
            ts_CONF_invalid(section, ENV_ACCURACY);
            goto err;
        }
    }
    if (!TS_RESP_CTX_set_accuracy(ctx, secs, millis, micros))
        goto err;

    ret = 1;
 err:
    sk_CONF_VALUE_pop_free(list, X509V3_conf_free);
    return ret;
}

int TS_CONF_set_clock_precision_digits(CONF *conf, const char *section,
                                       TS_RESP_CTX *ctx)
{
    int ret = 0;
    long digits = 0;

    /*
     * If not specified, set the default value to 0, i.e. sec precision
     */
    if (!NCONF_get_number_e(conf, section, ENV_CLOCK_PRECISION_DIGITS,
                            &digits))
        digits = 0;
    if (digits < 0 || digits > TS_MAX_CLOCK_PRECISION_DIGITS) {
        ts_CONF_invalid(section, ENV_CLOCK_PRECISION_DIGITS);
        goto err;
    }

    if (!TS_RESP_CTX_set_clock_precision_digits(ctx, digits))
        goto err;

    return 1;
 err:
    return ret;
}

static int ts_CONF_add_flag(CONF *conf, const char *section,
                            const char *field, int flag, TS_RESP_CTX *ctx)
{
    const char *value = NCONF_get_string(conf, section, field);

    if (value) {
        if (strcmp(value, ENV_VALUE_YES) == 0)
            TS_RESP_CTX_add_flags(ctx, flag);
        else if (strcmp(value, ENV_VALUE_NO) != 0) {
            ts_CONF_invalid(section, field);
            return 0;
        }
    }

    return 1;
}

int TS_CONF_set_ordering(CONF *conf, const char *section, TS_RESP_CTX *ctx)
{
    return ts_CONF_add_flag(conf, section, ENV_ORDERING, TS_ORDERING, ctx);
}

int TS_CONF_set_tsa_name(CONF *conf, const char *section, TS_RESP_CTX *ctx)
{
    return ts_CONF_add_flag(conf, section, ENV_TSA_NAME, TS_TSA_NAME, ctx);
}

int TS_CONF_set_ess_cert_id_chain(CONF *conf, const char *section,
                                  TS_RESP_CTX *ctx)
{
    return ts_CONF_add_flag(conf, section, ENV_ESS_CERT_ID_CHAIN,
                            TS_ESS_CERT_ID_CHAIN, ctx);
}

int TS_CONF_set_ess_cert_id_digest(CONF *conf, const char *section,
                                   TS_RESP_CTX *ctx)
{
    int ret = 0;
    const EVP_MD *cert_md = NULL;
    const char *md = NCONF_get_string(conf, section, ENV_ESS_CERT_ID_ALG);

    if (md == NULL)
        md = "sha1";

    cert_md = EVP_get_digestbyname(md);
    if (cert_md == NULL) {
        ts_CONF_invalid(section, ENV_ESS_CERT_ID_ALG);
        goto err;
    }

    if (!TS_RESP_CTX_set_ess_cert_id_digest(ctx, cert_md))
        goto err;

    ret = 1;
err:
    return ret;
}
