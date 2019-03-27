/*
 * Copyright 2001-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "eng_int.h"

/* Basic get/set stuff */

int ENGINE_set_load_privkey_function(ENGINE *e,
                                     ENGINE_LOAD_KEY_PTR loadpriv_f)
{
    e->load_privkey = loadpriv_f;
    return 1;
}

int ENGINE_set_load_pubkey_function(ENGINE *e, ENGINE_LOAD_KEY_PTR loadpub_f)
{
    e->load_pubkey = loadpub_f;
    return 1;
}

int ENGINE_set_load_ssl_client_cert_function(ENGINE *e,
                                             ENGINE_SSL_CLIENT_CERT_PTR
                                             loadssl_f)
{
    e->load_ssl_client_cert = loadssl_f;
    return 1;
}

ENGINE_LOAD_KEY_PTR ENGINE_get_load_privkey_function(const ENGINE *e)
{
    return e->load_privkey;
}

ENGINE_LOAD_KEY_PTR ENGINE_get_load_pubkey_function(const ENGINE *e)
{
    return e->load_pubkey;
}

ENGINE_SSL_CLIENT_CERT_PTR ENGINE_get_ssl_client_cert_function(const ENGINE
                                                               *e)
{
    return e->load_ssl_client_cert;
}

/* API functions to load public/private keys */

EVP_PKEY *ENGINE_load_private_key(ENGINE *e, const char *key_id,
                                  UI_METHOD *ui_method, void *callback_data)
{
    EVP_PKEY *pkey;

    if (e == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_LOAD_PRIVATE_KEY,
                  ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    CRYPTO_THREAD_write_lock(global_engine_lock);
    if (e->funct_ref == 0) {
        CRYPTO_THREAD_unlock(global_engine_lock);
        ENGINEerr(ENGINE_F_ENGINE_LOAD_PRIVATE_KEY, ENGINE_R_NOT_INITIALISED);
        return 0;
    }
    CRYPTO_THREAD_unlock(global_engine_lock);
    if (!e->load_privkey) {
        ENGINEerr(ENGINE_F_ENGINE_LOAD_PRIVATE_KEY,
                  ENGINE_R_NO_LOAD_FUNCTION);
        return 0;
    }
    pkey = e->load_privkey(e, key_id, ui_method, callback_data);
    if (!pkey) {
        ENGINEerr(ENGINE_F_ENGINE_LOAD_PRIVATE_KEY,
                  ENGINE_R_FAILED_LOADING_PRIVATE_KEY);
        return 0;
    }
    return pkey;
}

EVP_PKEY *ENGINE_load_public_key(ENGINE *e, const char *key_id,
                                 UI_METHOD *ui_method, void *callback_data)
{
    EVP_PKEY *pkey;

    if (e == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_LOAD_PUBLIC_KEY,
                  ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    CRYPTO_THREAD_write_lock(global_engine_lock);
    if (e->funct_ref == 0) {
        CRYPTO_THREAD_unlock(global_engine_lock);
        ENGINEerr(ENGINE_F_ENGINE_LOAD_PUBLIC_KEY, ENGINE_R_NOT_INITIALISED);
        return 0;
    }
    CRYPTO_THREAD_unlock(global_engine_lock);
    if (!e->load_pubkey) {
        ENGINEerr(ENGINE_F_ENGINE_LOAD_PUBLIC_KEY, ENGINE_R_NO_LOAD_FUNCTION);
        return 0;
    }
    pkey = e->load_pubkey(e, key_id, ui_method, callback_data);
    if (!pkey) {
        ENGINEerr(ENGINE_F_ENGINE_LOAD_PUBLIC_KEY,
                  ENGINE_R_FAILED_LOADING_PUBLIC_KEY);
        return 0;
    }
    return pkey;
}

int ENGINE_load_ssl_client_cert(ENGINE *e, SSL *s,
                                STACK_OF(X509_NAME) *ca_dn, X509 **pcert,
                                EVP_PKEY **ppkey, STACK_OF(X509) **pother,
                                UI_METHOD *ui_method, void *callback_data)
{

    if (e == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_LOAD_SSL_CLIENT_CERT,
                  ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    CRYPTO_THREAD_write_lock(global_engine_lock);
    if (e->funct_ref == 0) {
        CRYPTO_THREAD_unlock(global_engine_lock);
        ENGINEerr(ENGINE_F_ENGINE_LOAD_SSL_CLIENT_CERT,
                  ENGINE_R_NOT_INITIALISED);
        return 0;
    }
    CRYPTO_THREAD_unlock(global_engine_lock);
    if (!e->load_ssl_client_cert) {
        ENGINEerr(ENGINE_F_ENGINE_LOAD_SSL_CLIENT_CERT,
                  ENGINE_R_NO_LOAD_FUNCTION);
        return 0;
    }
    return e->load_ssl_client_cert(e, s, ca_dn, pcert, ppkey, pother,
                                   ui_method, callback_data);
}
