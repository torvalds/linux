/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdlib.h>
#include <string.h>

#include <openssl/conf.h>
#include <openssl/ct.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/safestack.h>

#include "internal/cryptlib.h"

/*
 * Information about a CT log server.
 */
struct ctlog_st {
    char *name;
    uint8_t log_id[CT_V1_HASHLEN];
    EVP_PKEY *public_key;
};

/*
 * A store for multiple CTLOG instances.
 * It takes ownership of any CTLOG instances added to it.
 */
struct ctlog_store_st {
    STACK_OF(CTLOG) *logs;
};

/* The context when loading a CT log list from a CONF file. */
typedef struct ctlog_store_load_ctx_st {
    CTLOG_STORE *log_store;
    CONF *conf;
    size_t invalid_log_entries;
} CTLOG_STORE_LOAD_CTX;

/*
 * Creates an empty context for loading a CT log store.
 * It should be populated before use.
 */
static CTLOG_STORE_LOAD_CTX *ctlog_store_load_ctx_new(void);

/*
 * Deletes a CT log store load context.
 * Does not delete any of the fields.
 */
static void ctlog_store_load_ctx_free(CTLOG_STORE_LOAD_CTX* ctx);

static CTLOG_STORE_LOAD_CTX *ctlog_store_load_ctx_new(void)
{
    CTLOG_STORE_LOAD_CTX *ctx = OPENSSL_zalloc(sizeof(*ctx));

    if (ctx == NULL)
        CTerr(CT_F_CTLOG_STORE_LOAD_CTX_NEW, ERR_R_MALLOC_FAILURE);

    return ctx;
}

static void ctlog_store_load_ctx_free(CTLOG_STORE_LOAD_CTX* ctx)
{
    OPENSSL_free(ctx);
}

/* Converts a log's public key into a SHA256 log ID */
static int ct_v1_log_id_from_pkey(EVP_PKEY *pkey,
                                  unsigned char log_id[CT_V1_HASHLEN])
{
    int ret = 0;
    unsigned char *pkey_der = NULL;
    int pkey_der_len = i2d_PUBKEY(pkey, &pkey_der);

    if (pkey_der_len <= 0) {
        CTerr(CT_F_CT_V1_LOG_ID_FROM_PKEY, CT_R_LOG_KEY_INVALID);
        goto err;
    }

    SHA256(pkey_der, pkey_der_len, log_id);
    ret = 1;
err:
    OPENSSL_free(pkey_der);
    return ret;
}

CTLOG_STORE *CTLOG_STORE_new(void)
{
    CTLOG_STORE *ret = OPENSSL_zalloc(sizeof(*ret));

    if (ret == NULL) {
        CTerr(CT_F_CTLOG_STORE_NEW, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    ret->logs = sk_CTLOG_new_null();
    if (ret->logs == NULL)
        goto err;

    return ret;
err:
    OPENSSL_free(ret);
    return NULL;
}

void CTLOG_STORE_free(CTLOG_STORE *store)
{
    if (store != NULL) {
        sk_CTLOG_pop_free(store->logs, CTLOG_free);
        OPENSSL_free(store);
    }
}

static int ctlog_new_from_conf(CTLOG **ct_log, const CONF *conf, const char *section)
{
    const char *description = NCONF_get_string(conf, section, "description");
    char *pkey_base64;

    if (description == NULL) {
        CTerr(CT_F_CTLOG_NEW_FROM_CONF, CT_R_LOG_CONF_MISSING_DESCRIPTION);
        return 0;
    }

    pkey_base64 = NCONF_get_string(conf, section, "key");
    if (pkey_base64 == NULL) {
        CTerr(CT_F_CTLOG_NEW_FROM_CONF, CT_R_LOG_CONF_MISSING_KEY);
        return 0;
    }

    return CTLOG_new_from_base64(ct_log, pkey_base64, description);
}

int CTLOG_STORE_load_default_file(CTLOG_STORE *store)
{
    const char *fpath = ossl_safe_getenv(CTLOG_FILE_EVP);

    if (fpath == NULL)
      fpath = CTLOG_FILE;

    return CTLOG_STORE_load_file(store, fpath);
}

/*
 * Called by CONF_parse_list, which stops if this returns <= 0,
 * Otherwise, one bad log entry would stop loading of any of
 * the following log entries.
 * It may stop parsing and returns -1 on any internal (malloc) error.
 */
static int ctlog_store_load_log(const char *log_name, int log_name_len,
                                void *arg)
{
    CTLOG_STORE_LOAD_CTX *load_ctx = arg;
    CTLOG *ct_log = NULL;
    /* log_name may not be null-terminated, so fix that before using it */
    char *tmp;
    int ret = 0;

    /* log_name will be NULL for empty list entries */
    if (log_name == NULL)
        return 1;

    tmp = OPENSSL_strndup(log_name, log_name_len);
    if (tmp == NULL)
        goto mem_err;

    ret = ctlog_new_from_conf(&ct_log, load_ctx->conf, tmp);
    OPENSSL_free(tmp);

    if (ret < 0) {
        /* Propagate any internal error */
        return ret;
    }
    if (ret == 0) {
        /* If we can't load this log, record that fact and skip it */
        ++load_ctx->invalid_log_entries;
        return 1;
    }

    if (!sk_CTLOG_push(load_ctx->log_store->logs, ct_log)) {
        goto mem_err;
    }
    return 1;

mem_err:
    CTLOG_free(ct_log);
    CTerr(CT_F_CTLOG_STORE_LOAD_LOG, ERR_R_MALLOC_FAILURE);
    return -1;
}

int CTLOG_STORE_load_file(CTLOG_STORE *store, const char *file)
{
    int ret = 0;
    char *enabled_logs;
    CTLOG_STORE_LOAD_CTX* load_ctx = ctlog_store_load_ctx_new();

    if (load_ctx == NULL)
        return 0;
    load_ctx->log_store = store;
    load_ctx->conf = NCONF_new(NULL);
    if (load_ctx->conf == NULL)
        goto end;

    if (NCONF_load(load_ctx->conf, file, NULL) <= 0) {
        CTerr(CT_F_CTLOG_STORE_LOAD_FILE, CT_R_LOG_CONF_INVALID);
        goto end;
    }

    enabled_logs = NCONF_get_string(load_ctx->conf, NULL, "enabled_logs");
    if (enabled_logs == NULL) {
        CTerr(CT_F_CTLOG_STORE_LOAD_FILE, CT_R_LOG_CONF_INVALID);
        goto end;
    }

    if (!CONF_parse_list(enabled_logs, ',', 1, ctlog_store_load_log, load_ctx) ||
        load_ctx->invalid_log_entries > 0) {
        CTerr(CT_F_CTLOG_STORE_LOAD_FILE, CT_R_LOG_CONF_INVALID);
        goto end;
    }

    ret = 1;
end:
    NCONF_free(load_ctx->conf);
    ctlog_store_load_ctx_free(load_ctx);
    return ret;
}

/*
 * Initialize a new CTLOG object.
 * Takes ownership of the public key.
 * Copies the name.
 */
CTLOG *CTLOG_new(EVP_PKEY *public_key, const char *name)
{
    CTLOG *ret = OPENSSL_zalloc(sizeof(*ret));

    if (ret == NULL) {
        CTerr(CT_F_CTLOG_NEW, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    ret->name = OPENSSL_strdup(name);
    if (ret->name == NULL) {
        CTerr(CT_F_CTLOG_NEW, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    if (ct_v1_log_id_from_pkey(public_key, ret->log_id) != 1)
        goto err;

    ret->public_key = public_key;
    return ret;
err:
    CTLOG_free(ret);
    return NULL;
}

/* Frees CT log and associated structures */
void CTLOG_free(CTLOG *log)
{
    if (log != NULL) {
        OPENSSL_free(log->name);
        EVP_PKEY_free(log->public_key);
        OPENSSL_free(log);
    }
}

const char *CTLOG_get0_name(const CTLOG *log)
{
    return log->name;
}

void CTLOG_get0_log_id(const CTLOG *log, const uint8_t **log_id,
                       size_t *log_id_len)
{
    *log_id = log->log_id;
    *log_id_len = CT_V1_HASHLEN;
}

EVP_PKEY *CTLOG_get0_public_key(const CTLOG *log)
{
    return log->public_key;
}

/*
 * Given a log ID, finds the matching log.
 * Returns NULL if no match found.
 */
const CTLOG *CTLOG_STORE_get0_log_by_id(const CTLOG_STORE *store,
                                        const uint8_t *log_id,
                                        size_t log_id_len)
{
    int i;

    for (i = 0; i < sk_CTLOG_num(store->logs); ++i) {
        const CTLOG *log = sk_CTLOG_value(store->logs, i);
        if (memcmp(log->log_id, log_id, log_id_len) == 0)
            return log;
    }

    return NULL;
}
