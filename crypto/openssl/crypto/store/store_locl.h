/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/thread_once.h"
#include <openssl/dsa.h>
#include <openssl/engine.h>
#include <openssl/evp.h>
#include <openssl/lhash.h>
#include <openssl/x509.h>
#include <openssl/store.h>

/*-
 *  OSSL_STORE_INFO stuff
 *  ---------------------
 */

struct ossl_store_info_st {
    int type;
    union {
        void *data;              /* used internally as generic pointer */

        struct {
            BUF_MEM *blob;
            char *pem_name;
        } embedded;              /* when type == OSSL_STORE_INFO_EMBEDDED */

        struct {
            char *name;
            char *desc;
        } name;                  /* when type == OSSL_STORE_INFO_NAME */

        EVP_PKEY *params;        /* when type == OSSL_STORE_INFO_PARAMS */
        EVP_PKEY *pkey;          /* when type == OSSL_STORE_INFO_PKEY */
        X509 *x509;              /* when type == OSSL_STORE_INFO_CERT */
        X509_CRL *crl;           /* when type == OSSL_STORE_INFO_CRL */
    } _;
};

DEFINE_STACK_OF(OSSL_STORE_INFO)

/*
 * EMBEDDED is a special type of OSSL_STORE_INFO, specially for the file
 * handlers.  It should never reach a calling application or any engine.
 * However, it can be used by a FILE_HANDLER's try_decode function to signal
 * that it has decoded the incoming blob into a new blob, and that the
 * attempted decoding should be immediately restarted with the new blob, using
 * the new PEM name.
 */
/*
 * Because this is an internal type, we don't make it public.
 */
#define OSSL_STORE_INFO_EMBEDDED       -1
OSSL_STORE_INFO *ossl_store_info_new_EMBEDDED(const char *new_pem_name,
                                              BUF_MEM *embedded);
BUF_MEM *ossl_store_info_get0_EMBEDDED_buffer(OSSL_STORE_INFO *info);
char *ossl_store_info_get0_EMBEDDED_pem_name(OSSL_STORE_INFO *info);

/*-
 *  OSSL_STORE_SEARCH stuff
 *  -----------------------
 */

struct ossl_store_search_st {
    int search_type;

    /*
     * Used by OSSL_STORE_SEARCH_BY_NAME and
     * OSSL_STORE_SEARCH_BY_ISSUER_SERIAL
     */
    X509_NAME *name;

    /* Used by OSSL_STORE_SEARCH_BY_ISSUER_SERIAL */
    const ASN1_INTEGER *serial;

    /* Used by OSSL_STORE_SEARCH_BY_KEY_FINGERPRINT */
    const EVP_MD *digest;

    /*
     * Used by OSSL_STORE_SEARCH_BY_KEY_FINGERPRINT and
     * OSSL_STORE_SEARCH_BY_ALIAS
     */
    const unsigned char *string;
    size_t stringlength;
};

/*-
 *  OSSL_STORE_LOADER stuff
 *  -----------------------
 */

int ossl_store_register_loader_int(OSSL_STORE_LOADER *loader);
OSSL_STORE_LOADER *ossl_store_unregister_loader_int(const char *scheme);

/* loader stuff */
struct ossl_store_loader_st {
    const char *scheme;
    ENGINE *engine;
    OSSL_STORE_open_fn open;
    OSSL_STORE_ctrl_fn ctrl;
    OSSL_STORE_expect_fn expect;
    OSSL_STORE_find_fn find;
    OSSL_STORE_load_fn load;
    OSSL_STORE_eof_fn eof;
    OSSL_STORE_error_fn error;
    OSSL_STORE_close_fn close;
};
DEFINE_LHASH_OF(OSSL_STORE_LOADER);

const OSSL_STORE_LOADER *ossl_store_get0_loader_int(const char *scheme);
void ossl_store_destroy_loaders_int(void);

/*-
 *  OSSL_STORE init stuff
 *  ---------------------
 */

int ossl_store_init_once(void);
int ossl_store_file_loader_init(void);

/*-
 *  'file' scheme stuff
 *  -------------------
 */

OSSL_STORE_LOADER_CTX *ossl_store_file_attach_pem_bio_int(BIO *bp);
int ossl_store_file_detach_pem_bio_int(OSSL_STORE_LOADER_CTX *ctx);
