/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "e_os.h"
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include <assert.h>

#include <openssl/bio.h>
#include <openssl/dsa.h>         /* For d2i_DSAPrivateKey */
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>      /* For the PKCS8 stuff o.O */
#include <openssl/rsa.h>         /* For d2i_RSAPrivateKey */
#include <openssl/safestack.h>
#include <openssl/store.h>
#include <openssl/ui.h>
#include <openssl/x509.h>        /* For the PKCS8 stuff o.O */
#include "internal/asn1_int.h"
#include "internal/ctype.h"
#include "internal/o_dir.h"
#include "internal/cryptlib.h"
#include "internal/store_int.h"
#include "store_locl.h"

#ifdef _WIN32
# define stat    _stat
#endif

#ifndef S_ISDIR
# define S_ISDIR(a) (((a) & S_IFMT) == S_IFDIR)
#endif

/*-
 *  Password prompting
 *  ------------------
 */

static char *file_get_pass(const UI_METHOD *ui_method, char *pass,
                           size_t maxsize, const char *prompt_info, void *data)
{
    UI *ui = UI_new();
    char *prompt = NULL;

    if (ui == NULL) {
        OSSL_STOREerr(OSSL_STORE_F_FILE_GET_PASS, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    if (ui_method != NULL)
        UI_set_method(ui, ui_method);
    UI_add_user_data(ui, data);

    if ((prompt = UI_construct_prompt(ui, "pass phrase",
                                      prompt_info)) == NULL) {
        OSSL_STOREerr(OSSL_STORE_F_FILE_GET_PASS, ERR_R_MALLOC_FAILURE);
        pass = NULL;
    } else if (!UI_add_input_string(ui, prompt, UI_INPUT_FLAG_DEFAULT_PWD,
                                    pass, 0, maxsize - 1)) {
        OSSL_STOREerr(OSSL_STORE_F_FILE_GET_PASS, ERR_R_UI_LIB);
        pass = NULL;
    } else {
        switch (UI_process(ui)) {
        case -2:
            OSSL_STOREerr(OSSL_STORE_F_FILE_GET_PASS,
                          OSSL_STORE_R_UI_PROCESS_INTERRUPTED_OR_CANCELLED);
            pass = NULL;
            break;
        case -1:
            OSSL_STOREerr(OSSL_STORE_F_FILE_GET_PASS, ERR_R_UI_LIB);
            pass = NULL;
            break;
        default:
            break;
        }
    }

    OPENSSL_free(prompt);
    UI_free(ui);
    return pass;
}

struct pem_pass_data {
    const UI_METHOD *ui_method;
    void *data;
    const char *prompt_info;
};

static int file_fill_pem_pass_data(struct pem_pass_data *pass_data,
                                   const char *prompt_info,
                                   const UI_METHOD *ui_method, void *ui_data)
{
    if (pass_data == NULL)
        return 0;
    pass_data->ui_method = ui_method;
    pass_data->data = ui_data;
    pass_data->prompt_info = prompt_info;
    return 1;
}

/* This is used anywhere a pem_password_cb is needed */
static int file_get_pem_pass(char *buf, int num, int w, void *data)
{
    struct pem_pass_data *pass_data = data;
    char *pass = file_get_pass(pass_data->ui_method, buf, num,
                               pass_data->prompt_info, pass_data->data);

    return pass == NULL ? 0 : strlen(pass);
}

/*-
 *  The file scheme decoders
 *  ------------------------
 *
 *  Each possible data type has its own decoder, which either operates
 *  through a given PEM name, or attempts to decode to see if the blob
 *  it's given is decodable for its data type.  The assumption is that
 *  only the correct data type will match the content.
 */

/*-
 * The try_decode function is called to check if the blob of data can
 * be used by this handler, and if it can, decodes it into a supported
 * OpenSSL type and returns a OSSL_STORE_INFO with the decoded data.
 * Input:
 *    pem_name:     If this blob comes from a PEM file, this holds
 *                  the PEM name.  If it comes from another type of
 *                  file, this is NULL.
 *    pem_header:   If this blob comes from a PEM file, this holds
 *                  the PEM headers.  If it comes from another type of
 *                  file, this is NULL.
 *    blob:         The blob of data to match with what this handler
 *                  can use.
 *    len:          The length of the blob.
 *    handler_ctx:  For a handler marked repeatable, this pointer can
 *                  be used to create a context for the handler.  IT IS
 *                  THE HANDLER'S RESPONSIBILITY TO CREATE AND DESTROY
 *                  THIS CONTEXT APPROPRIATELY, i.e. create on first call
 *                  and destroy when about to return NULL.
 *    matchcount:   A pointer to an int to count matches for this data.
 *                  Usually becomes 0 (no match) or 1 (match!), but may
 *                  be higher in the (unlikely) event that the data matches
 *                  more than one possibility.  The int will always be
 *                  zero when the function is called.
 *    ui_method:    Application UI method for getting a password, pin
 *                  or any other interactive data.
 *    ui_data:      Application data to be passed to ui_method when
 *                  it's called.
 * Output:
 *    a OSSL_STORE_INFO
 */
typedef OSSL_STORE_INFO *(*file_try_decode_fn)(const char *pem_name,
                                               const char *pem_header,
                                               const unsigned char *blob,
                                               size_t len, void **handler_ctx,
                                               int *matchcount,
                                               const UI_METHOD *ui_method,
                                               void *ui_data);
/*
 * The eof function should return 1 if there's no more data to be found
 * with the handler_ctx, otherwise 0.  This is only used when the handler is
 * marked repeatable.
 */
typedef int (*file_eof_fn)(void *handler_ctx);
/*
 * The destroy_ctx function is used to destroy the handler_ctx that was
 * intiated by a repeatable try_decode fuction.  This is only used when
 * the handler is marked repeatable.
 */
typedef void (*file_destroy_ctx_fn)(void **handler_ctx);

typedef struct file_handler_st {
    const char *name;
    file_try_decode_fn try_decode;
    file_eof_fn eof;
    file_destroy_ctx_fn destroy_ctx;

    /* flags */
    int repeatable;
} FILE_HANDLER;

/*
 * PKCS#12 decoder.  It operates by decoding all of the blob content,
 * extracting all the interesting data from it and storing them internally,
 * then serving them one piece at a time.
 */
static OSSL_STORE_INFO *try_decode_PKCS12(const char *pem_name,
                                          const char *pem_header,
                                          const unsigned char *blob,
                                          size_t len, void **pctx,
                                          int *matchcount,
                                          const UI_METHOD *ui_method,
                                          void *ui_data)
{
    OSSL_STORE_INFO *store_info = NULL;
    STACK_OF(OSSL_STORE_INFO) *ctx = *pctx;

    if (ctx == NULL) {
        /* Initial parsing */
        PKCS12 *p12;
        int ok = 0;

        if (pem_name != NULL)
            /* No match, there is no PEM PKCS12 tag */
            return NULL;

        if ((p12 = d2i_PKCS12(NULL, &blob, len)) != NULL) {
            char *pass = NULL;
            char tpass[PEM_BUFSIZE];
            EVP_PKEY *pkey = NULL;
            X509 *cert = NULL;
            STACK_OF(X509) *chain = NULL;

            *matchcount = 1;

            if (PKCS12_verify_mac(p12, "", 0)
                || PKCS12_verify_mac(p12, NULL, 0)) {
                pass = "";
            } else {
                if ((pass = file_get_pass(ui_method, tpass, PEM_BUFSIZE,
                                          "PKCS12 import password",
                                          ui_data)) == NULL) {
                    OSSL_STOREerr(OSSL_STORE_F_TRY_DECODE_PKCS12,
                                  OSSL_STORE_R_PASSPHRASE_CALLBACK_ERROR);
                    goto p12_end;
                }
                if (!PKCS12_verify_mac(p12, pass, strlen(pass))) {
                    OSSL_STOREerr(OSSL_STORE_F_TRY_DECODE_PKCS12,
                                  OSSL_STORE_R_ERROR_VERIFYING_PKCS12_MAC);
                    goto p12_end;
                }
            }

            if (PKCS12_parse(p12, pass, &pkey, &cert, &chain)) {
                OSSL_STORE_INFO *osi_pkey = NULL;
                OSSL_STORE_INFO *osi_cert = NULL;
                OSSL_STORE_INFO *osi_ca = NULL;

                if ((ctx = sk_OSSL_STORE_INFO_new_null()) != NULL
                    && (osi_pkey = OSSL_STORE_INFO_new_PKEY(pkey)) != NULL
                    && sk_OSSL_STORE_INFO_push(ctx, osi_pkey) != 0
                    && (osi_cert = OSSL_STORE_INFO_new_CERT(cert)) != NULL
                    && sk_OSSL_STORE_INFO_push(ctx, osi_cert) != 0) {
                    ok = 1;
                    osi_pkey = NULL;
                    osi_cert = NULL;

                    while(sk_X509_num(chain) > 0) {
                        X509 *ca = sk_X509_value(chain, 0);

                        if ((osi_ca = OSSL_STORE_INFO_new_CERT(ca)) == NULL
                            || sk_OSSL_STORE_INFO_push(ctx, osi_ca) == 0) {
                            ok = 0;
                            break;
                        }
                        osi_ca = NULL;
                        (void)sk_X509_shift(chain);
                    }
                }
                if (!ok) {
                    OSSL_STORE_INFO_free(osi_ca);
                    OSSL_STORE_INFO_free(osi_cert);
                    OSSL_STORE_INFO_free(osi_pkey);
                    sk_OSSL_STORE_INFO_pop_free(ctx, OSSL_STORE_INFO_free);
                    EVP_PKEY_free(pkey);
                    X509_free(cert);
                    sk_X509_pop_free(chain, X509_free);
                    ctx = NULL;
                }
                *pctx = ctx;
            }
        }
     p12_end:
        PKCS12_free(p12);
        if (!ok)
            return NULL;
    }

    if (ctx != NULL) {
        *matchcount = 1;
        store_info = sk_OSSL_STORE_INFO_shift(ctx);
    }

    return store_info;
}

static int eof_PKCS12(void *ctx_)
{
    STACK_OF(OSSL_STORE_INFO) *ctx = ctx_;

    return ctx == NULL || sk_OSSL_STORE_INFO_num(ctx) == 0;
}

static void destroy_ctx_PKCS12(void **pctx)
{
    STACK_OF(OSSL_STORE_INFO) *ctx = *pctx;

    sk_OSSL_STORE_INFO_pop_free(ctx, OSSL_STORE_INFO_free);
    *pctx = NULL;
}

static FILE_HANDLER PKCS12_handler = {
    "PKCS12",
    try_decode_PKCS12,
    eof_PKCS12,
    destroy_ctx_PKCS12,
    1                            /* repeatable */
};

/*
 * Encrypted PKCS#8 decoder.  It operates by just decrypting the given blob
 * into a new blob, which is returned as an EMBEDDED STORE_INFO.  The whole
 * decoding process will then start over with the new blob.
 */
static OSSL_STORE_INFO *try_decode_PKCS8Encrypted(const char *pem_name,
                                                  const char *pem_header,
                                                  const unsigned char *blob,
                                                  size_t len, void **pctx,
                                                  int *matchcount,
                                                  const UI_METHOD *ui_method,
                                                  void *ui_data)
{
    X509_SIG *p8 = NULL;
    char kbuf[PEM_BUFSIZE];
    char *pass = NULL;
    const X509_ALGOR *dalg = NULL;
    const ASN1_OCTET_STRING *doct = NULL;
    OSSL_STORE_INFO *store_info = NULL;
    BUF_MEM *mem = NULL;
    unsigned char *new_data = NULL;
    int new_data_len;

    if (pem_name != NULL) {
        if (strcmp(pem_name, PEM_STRING_PKCS8) != 0)
            return NULL;
        *matchcount = 1;
    }

    if ((p8 = d2i_X509_SIG(NULL, &blob, len)) == NULL)
        return NULL;

    *matchcount = 1;

    if ((mem = BUF_MEM_new()) == NULL) {
        OSSL_STOREerr(OSSL_STORE_F_TRY_DECODE_PKCS8ENCRYPTED,
                      ERR_R_MALLOC_FAILURE);
        goto nop8;
    }

    if ((pass = file_get_pass(ui_method, kbuf, PEM_BUFSIZE,
                              "PKCS8 decrypt password", ui_data)) == NULL) {
        OSSL_STOREerr(OSSL_STORE_F_TRY_DECODE_PKCS8ENCRYPTED,
                      OSSL_STORE_R_BAD_PASSWORD_READ);
        goto nop8;
    }

    X509_SIG_get0(p8, &dalg, &doct);
    if (!PKCS12_pbe_crypt(dalg, pass, strlen(pass), doct->data, doct->length,
                          &new_data, &new_data_len, 0))
        goto nop8;

    mem->data = (char *)new_data;
    mem->max = mem->length = (size_t)new_data_len;
    X509_SIG_free(p8);

    store_info = ossl_store_info_new_EMBEDDED(PEM_STRING_PKCS8INF, mem);
    if (store_info == NULL) {
        OSSL_STOREerr(OSSL_STORE_F_TRY_DECODE_PKCS8ENCRYPTED,
                      ERR_R_MALLOC_FAILURE);
        goto nop8;
    }

    return store_info;
 nop8:
    X509_SIG_free(p8);
    BUF_MEM_free(mem);
    return NULL;
}

static FILE_HANDLER PKCS8Encrypted_handler = {
    "PKCS8Encrypted",
    try_decode_PKCS8Encrypted
};

/*
 * Private key decoder.  Decodes all sorts of private keys, both PKCS#8
 * encoded ones and old style PEM ones (with the key type is encoded into
 * the PEM name).
 */
int pem_check_suffix(const char *pem_str, const char *suffix);
static OSSL_STORE_INFO *try_decode_PrivateKey(const char *pem_name,
                                              const char *pem_header,
                                              const unsigned char *blob,
                                              size_t len, void **pctx,
                                              int *matchcount,
                                              const UI_METHOD *ui_method,
                                              void *ui_data)
{
    OSSL_STORE_INFO *store_info = NULL;
    EVP_PKEY *pkey = NULL;
    const EVP_PKEY_ASN1_METHOD *ameth = NULL;

    if (pem_name != NULL) {
        if (strcmp(pem_name, PEM_STRING_PKCS8INF) == 0) {
            PKCS8_PRIV_KEY_INFO *p8inf =
                d2i_PKCS8_PRIV_KEY_INFO(NULL, &blob, len);

            *matchcount = 1;
            if (p8inf != NULL)
                pkey = EVP_PKCS82PKEY(p8inf);
            PKCS8_PRIV_KEY_INFO_free(p8inf);
        } else {
            int slen;

            if ((slen = pem_check_suffix(pem_name, "PRIVATE KEY")) > 0
                && (ameth = EVP_PKEY_asn1_find_str(NULL, pem_name,
                                                   slen)) != NULL) {
                *matchcount = 1;
                pkey = d2i_PrivateKey(ameth->pkey_id, NULL, &blob, len);
            }
        }
    } else {
        int i;

        for (i = 0; i < EVP_PKEY_asn1_get_count(); i++) {
            EVP_PKEY *tmp_pkey = NULL;
            const unsigned char *tmp_blob = blob;

            ameth = EVP_PKEY_asn1_get0(i);
            if (ameth->pkey_flags & ASN1_PKEY_ALIAS)
                continue;

            tmp_pkey = d2i_PrivateKey(ameth->pkey_id, NULL, &tmp_blob, len);
            if (tmp_pkey != NULL) {
                if (pkey != NULL)
                    EVP_PKEY_free(tmp_pkey);
                else
                    pkey = tmp_pkey;
                (*matchcount)++;
            }
        }

        if (*matchcount > 1) {
            EVP_PKEY_free(pkey);
            pkey = NULL;
        }
    }
    if (pkey == NULL)
        /* No match */
        return NULL;

    store_info = OSSL_STORE_INFO_new_PKEY(pkey);
    if (store_info == NULL)
        EVP_PKEY_free(pkey);

    return store_info;
}

static FILE_HANDLER PrivateKey_handler = {
    "PrivateKey",
    try_decode_PrivateKey
};

/*
 * Public key decoder.  Only supports SubjectPublicKeyInfo formated keys.
 */
static OSSL_STORE_INFO *try_decode_PUBKEY(const char *pem_name,
                                          const char *pem_header,
                                          const unsigned char *blob,
                                          size_t len, void **pctx,
                                          int *matchcount,
                                          const UI_METHOD *ui_method,
                                          void *ui_data)
{
    OSSL_STORE_INFO *store_info = NULL;
    EVP_PKEY *pkey = NULL;

    if (pem_name != NULL) {
        if (strcmp(pem_name, PEM_STRING_PUBLIC) != 0)
            /* No match */
            return NULL;
        *matchcount = 1;
    }

    if ((pkey = d2i_PUBKEY(NULL, &blob, len)) != NULL) {
        *matchcount = 1;
        store_info = OSSL_STORE_INFO_new_PKEY(pkey);
    }

    return store_info;
}

static FILE_HANDLER PUBKEY_handler = {
    "PUBKEY",
    try_decode_PUBKEY
};

/*
 * Key parameter decoder.
 */
static OSSL_STORE_INFO *try_decode_params(const char *pem_name,
                                          const char *pem_header,
                                          const unsigned char *blob,
                                          size_t len, void **pctx,
                                          int *matchcount,
                                          const UI_METHOD *ui_method,
                                          void *ui_data)
{
    OSSL_STORE_INFO *store_info = NULL;
    int slen = 0;
    EVP_PKEY *pkey = NULL;
    const EVP_PKEY_ASN1_METHOD *ameth = NULL;
    int ok = 0;

    if (pem_name != NULL) {
        if ((slen = pem_check_suffix(pem_name, "PARAMETERS")) == 0)
            return NULL;
        *matchcount = 1;
    }

    if (slen > 0) {
        if ((pkey = EVP_PKEY_new()) == NULL) {
            OSSL_STOREerr(OSSL_STORE_F_TRY_DECODE_PARAMS, ERR_R_EVP_LIB);
            return NULL;
        }


        if (EVP_PKEY_set_type_str(pkey, pem_name, slen)
            && (ameth = EVP_PKEY_get0_asn1(pkey)) != NULL
            && ameth->param_decode != NULL
            && ameth->param_decode(pkey, &blob, len))
            ok = 1;
    } else {
        int i;
        EVP_PKEY *tmp_pkey = NULL;

        for (i = 0; i < EVP_PKEY_asn1_get_count(); i++) {
            const unsigned char *tmp_blob = blob;

            if (tmp_pkey == NULL && (tmp_pkey = EVP_PKEY_new()) == NULL) {
                OSSL_STOREerr(OSSL_STORE_F_TRY_DECODE_PARAMS, ERR_R_EVP_LIB);
                break;
            }

            ameth = EVP_PKEY_asn1_get0(i);
            if (ameth->pkey_flags & ASN1_PKEY_ALIAS)
                continue;

            if (EVP_PKEY_set_type(tmp_pkey, ameth->pkey_id)
                && (ameth = EVP_PKEY_get0_asn1(tmp_pkey)) != NULL
                && ameth->param_decode != NULL
                && ameth->param_decode(tmp_pkey, &tmp_blob, len)) {
                if (pkey != NULL)
                    EVP_PKEY_free(tmp_pkey);
                else
                    pkey = tmp_pkey;
                tmp_pkey = NULL;
                (*matchcount)++;
            }
        }

        EVP_PKEY_free(tmp_pkey);
        if (*matchcount == 1) {
            ok = 1;
        }
    }

    if (ok)
        store_info = OSSL_STORE_INFO_new_PARAMS(pkey);
    if (store_info == NULL)
        EVP_PKEY_free(pkey);

    return store_info;
}

static FILE_HANDLER params_handler = {
    "params",
    try_decode_params
};

/*
 * X.509 certificate decoder.
 */
static OSSL_STORE_INFO *try_decode_X509Certificate(const char *pem_name,
                                                   const char *pem_header,
                                                   const unsigned char *blob,
                                                   size_t len, void **pctx,
                                                   int *matchcount,
                                                   const UI_METHOD *ui_method,
                                                   void *ui_data)
{
    OSSL_STORE_INFO *store_info = NULL;
    X509 *cert = NULL;

    /*
     * In most cases, we can try to interpret the serialized data as a trusted
     * cert (X509 + X509_AUX) and fall back to reading it as a normal cert
     * (just X509), but if the PEM name specifically declares it as a trusted
     * cert, then no fallback should be engaged.  |ignore_trusted| tells if
     * the fallback can be used (1) or not (0).
     */
    int ignore_trusted = 1;

    if (pem_name != NULL) {
        if (strcmp(pem_name, PEM_STRING_X509_TRUSTED) == 0)
            ignore_trusted = 0;
        else if (strcmp(pem_name, PEM_STRING_X509_OLD) != 0
                 && strcmp(pem_name, PEM_STRING_X509) != 0)
            /* No match */
            return NULL;
        *matchcount = 1;
    }

    if ((cert = d2i_X509_AUX(NULL, &blob, len)) != NULL
        || (ignore_trusted && (cert = d2i_X509(NULL, &blob, len)) != NULL)) {
        *matchcount = 1;
        store_info = OSSL_STORE_INFO_new_CERT(cert);
    }

    if (store_info == NULL)
        X509_free(cert);

    return store_info;
}

static FILE_HANDLER X509Certificate_handler = {
    "X509Certificate",
    try_decode_X509Certificate
};

/*
 * X.509 CRL decoder.
 */
static OSSL_STORE_INFO *try_decode_X509CRL(const char *pem_name,
                                           const char *pem_header,
                                           const unsigned char *blob,
                                           size_t len, void **pctx,
                                           int *matchcount,
                                           const UI_METHOD *ui_method,
                                           void *ui_data)
{
    OSSL_STORE_INFO *store_info = NULL;
    X509_CRL *crl = NULL;

    if (pem_name != NULL) {
        if (strcmp(pem_name, PEM_STRING_X509_CRL) != 0)
            /* No match */
            return NULL;
        *matchcount = 1;
    }

    if ((crl = d2i_X509_CRL(NULL, &blob, len)) != NULL) {
        *matchcount = 1;
        store_info = OSSL_STORE_INFO_new_CRL(crl);
    }

    if (store_info == NULL)
        X509_CRL_free(crl);

    return store_info;
}

static FILE_HANDLER X509CRL_handler = {
    "X509CRL",
    try_decode_X509CRL
};

/*
 * To finish it all off, we collect all the handlers.
 */
static const FILE_HANDLER *file_handlers[] = {
    &PKCS12_handler,
    &PKCS8Encrypted_handler,
    &X509Certificate_handler,
    &X509CRL_handler,
    &params_handler,
    &PUBKEY_handler,
    &PrivateKey_handler,
};


/*-
 *  The loader itself
 *  -----------------
 */

struct ossl_store_loader_ctx_st {
    enum {
        is_raw = 0,
        is_pem,
        is_dir
    } type;
    int errcnt;
#define FILE_FLAG_SECMEM         (1<<0)
    unsigned int flags;
    union {
        struct {                 /* Used with is_raw and is_pem */
            BIO *file;

            /*
             * The following are used when the handler is marked as
             * repeatable
             */
            const FILE_HANDLER *last_handler;
            void *last_handler_ctx;
        } file;
        struct {                 /* Used with is_dir */
            OPENSSL_DIR_CTX *ctx;
            int end_reached;
            char *uri;

            /*
             * When a search expression is given, these are filled in.
             * |search_name| contains the file basename to look for.
             * The string is exactly 8 characters long.
             */
            char search_name[9];

            /*
             * The directory reading utility we have combines opening with
             * reading the first name.  To make sure we can detect the end
             * at the right time, we read early and cache the name.
             */
            const char *last_entry;
            int last_errno;
        } dir;
    } _;

    /* Expected object type.  May be unspecified */
    int expected_type;
};

static void OSSL_STORE_LOADER_CTX_free(OSSL_STORE_LOADER_CTX *ctx)
{
    if (ctx->type == is_dir) {
        OPENSSL_free(ctx->_.dir.uri);
    } else {
        if (ctx->_.file.last_handler != NULL) {
            ctx->_.file.last_handler->destroy_ctx(&ctx->_.file.last_handler_ctx);
            ctx->_.file.last_handler_ctx = NULL;
            ctx->_.file.last_handler = NULL;
        }
    }
    OPENSSL_free(ctx);
}

static OSSL_STORE_LOADER_CTX *file_open(const OSSL_STORE_LOADER *loader,
                                        const char *uri,
                                        const UI_METHOD *ui_method,
                                        void *ui_data)
{
    OSSL_STORE_LOADER_CTX *ctx = NULL;
    struct stat st;
    struct {
        const char *path;
        unsigned int check_absolute:1;
    } path_data[2];
    size_t path_data_n = 0, i;
    const char *path;

    /*
     * First step, just take the URI as is.
     */
    path_data[path_data_n].check_absolute = 0;
    path_data[path_data_n++].path = uri;

    /*
     * Second step, if the URI appears to start with the 'file' scheme,
     * extract the path and make that the second path to check.
     * There's a special case if the URI also contains an authority, then
     * the full URI shouldn't be used as a path anywhere.
     */
    if (strncasecmp(uri, "file:", 5) == 0) {
        const char *p = &uri[5];

        if (strncmp(&uri[5], "//", 2) == 0) {
            path_data_n--;           /* Invalidate using the full URI */
            if (strncasecmp(&uri[7], "localhost/", 10) == 0) {
                p = &uri[16];
            } else if (uri[7] == '/') {
                p = &uri[7];
            } else {
                OSSL_STOREerr(OSSL_STORE_F_FILE_OPEN,
                              OSSL_STORE_R_URI_AUTHORITY_UNSUPPORTED);
                return NULL;
            }
        }

        path_data[path_data_n].check_absolute = 1;
#ifdef _WIN32
        /* Windows file: URIs with a drive letter start with a / */
        if (p[0] == '/' && p[2] == ':' && p[3] == '/') {
            char c = ossl_tolower(p[1]);

            if (c >= 'a' && c <= 'z') {
                p++;
                /* We know it's absolute, so no need to check */
                path_data[path_data_n].check_absolute = 0;
            }
        }
#endif
        path_data[path_data_n++].path = p;
    }


    for (i = 0, path = NULL; path == NULL && i < path_data_n; i++) {
        /*
         * If the scheme "file" was an explicit part of the URI, the path must
         * be absolute.  So says RFC 8089
         */
        if (path_data[i].check_absolute && path_data[i].path[0] != '/') {
            OSSL_STOREerr(OSSL_STORE_F_FILE_OPEN,
                          OSSL_STORE_R_PATH_MUST_BE_ABSOLUTE);
            ERR_add_error_data(1, path_data[i].path);
            return NULL;
        }

        if (stat(path_data[i].path, &st) < 0) {
            SYSerr(SYS_F_STAT, errno);
            ERR_add_error_data(1, path_data[i].path);
        } else {
            path = path_data[i].path;
        }
    }
    if (path == NULL) {
        return NULL;
    }

    /* Successfully found a working path, clear possible collected errors */
    ERR_clear_error();

    ctx = OPENSSL_zalloc(sizeof(*ctx));
    if (ctx == NULL) {
        OSSL_STOREerr(OSSL_STORE_F_FILE_OPEN, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    if (S_ISDIR(st.st_mode)) {
        /*
         * Try to copy everything, even if we know that some of them must be
         * NULL for the moment.  This prevents errors in the future, when more
         * components may be used.
         */
        ctx->_.dir.uri = OPENSSL_strdup(uri);
        ctx->type = is_dir;

        if (ctx->_.dir.uri == NULL)
            goto err;

        ctx->_.dir.last_entry = OPENSSL_DIR_read(&ctx->_.dir.ctx, path);
        ctx->_.dir.last_errno = errno;
        if (ctx->_.dir.last_entry == NULL) {
            if (ctx->_.dir.last_errno != 0) {
                char errbuf[256];
                errno = ctx->_.dir.last_errno;
                openssl_strerror_r(errno, errbuf, sizeof(errbuf));
                OSSL_STOREerr(OSSL_STORE_F_FILE_OPEN, ERR_R_SYS_LIB);
                ERR_add_error_data(1, errbuf);
                goto err;
            }
            ctx->_.dir.end_reached = 1;
        }
    } else {
        BIO *buff = NULL;
        char peekbuf[4096] = { 0, };

        if ((buff = BIO_new(BIO_f_buffer())) == NULL
            || (ctx->_.file.file = BIO_new_file(path, "rb")) == NULL) {
            BIO_free_all(buff);
            goto err;
        }

        ctx->_.file.file = BIO_push(buff, ctx->_.file.file);
        if (BIO_buffer_peek(ctx->_.file.file, peekbuf, sizeof(peekbuf) - 1) > 0) {
            peekbuf[sizeof(peekbuf) - 1] = '\0';
            if (strstr(peekbuf, "-----BEGIN ") != NULL)
                ctx->type = is_pem;
        }
    }

    return ctx;
 err:
    OSSL_STORE_LOADER_CTX_free(ctx);
    return NULL;
}

static int file_ctrl(OSSL_STORE_LOADER_CTX *ctx, int cmd, va_list args)
{
    int ret = 1;

    switch (cmd) {
    case OSSL_STORE_C_USE_SECMEM:
        {
            int on = *(va_arg(args, int *));

            switch (on) {
            case 0:
                ctx->flags &= ~FILE_FLAG_SECMEM;
                break;
            case 1:
                ctx->flags |= FILE_FLAG_SECMEM;
                break;
            default:
                OSSL_STOREerr(OSSL_STORE_F_FILE_CTRL,
                              ERR_R_PASSED_INVALID_ARGUMENT);
                ret = 0;
                break;
            }
        }
        break;
    default:
        break;
    }

    return ret;
}

static int file_expect(OSSL_STORE_LOADER_CTX *ctx, int expected)
{
    ctx->expected_type = expected;
    return 1;
}

static int file_find(OSSL_STORE_LOADER_CTX *ctx, OSSL_STORE_SEARCH *search)
{
    /*
     * If ctx == NULL, the library is looking to know if this loader supports
     * the given search type.
     */

    if (OSSL_STORE_SEARCH_get_type(search) == OSSL_STORE_SEARCH_BY_NAME) {
        unsigned long hash = 0;

        if (ctx == NULL)
            return 1;

        if (ctx->type != is_dir) {
            OSSL_STOREerr(OSSL_STORE_F_FILE_FIND,
                          OSSL_STORE_R_SEARCH_ONLY_SUPPORTED_FOR_DIRECTORIES);
            return 0;
        }

        hash = X509_NAME_hash(OSSL_STORE_SEARCH_get0_name(search));
        BIO_snprintf(ctx->_.dir.search_name, sizeof(ctx->_.dir.search_name),
                     "%08lx", hash);
        return 1;
    }

    if (ctx != NULL)
        OSSL_STOREerr(OSSL_STORE_F_FILE_FIND,
                      OSSL_STORE_R_UNSUPPORTED_SEARCH_TYPE);
    return 0;
}

/* Internal function to decode an already opened PEM file */
OSSL_STORE_LOADER_CTX *ossl_store_file_attach_pem_bio_int(BIO *bp)
{
    OSSL_STORE_LOADER_CTX *ctx = OPENSSL_zalloc(sizeof(*ctx));

    if (ctx == NULL) {
        OSSL_STOREerr(OSSL_STORE_F_OSSL_STORE_FILE_ATTACH_PEM_BIO_INT,
                      ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    ctx->_.file.file = bp;
    ctx->type = is_pem;

    return ctx;
}

static OSSL_STORE_INFO *file_load_try_decode(OSSL_STORE_LOADER_CTX *ctx,
                                             const char *pem_name,
                                             const char *pem_header,
                                             unsigned char *data, size_t len,
                                             const UI_METHOD *ui_method,
                                             void *ui_data, int *matchcount)
{
    OSSL_STORE_INFO *result = NULL;
    BUF_MEM *new_mem = NULL;
    char *new_pem_name = NULL;
    int t = 0;

 again:
    {
        size_t i = 0;
        void *handler_ctx = NULL;
        const FILE_HANDLER **matching_handlers =
            OPENSSL_zalloc(sizeof(*matching_handlers)
                           * OSSL_NELEM(file_handlers));

        if (matching_handlers == NULL) {
            OSSL_STOREerr(OSSL_STORE_F_FILE_LOAD_TRY_DECODE,
                          ERR_R_MALLOC_FAILURE);
            goto err;
        }

        *matchcount = 0;
        for (i = 0; i < OSSL_NELEM(file_handlers); i++) {
            const FILE_HANDLER *handler = file_handlers[i];
            int try_matchcount = 0;
            void *tmp_handler_ctx = NULL;
            OSSL_STORE_INFO *tmp_result =
                handler->try_decode(pem_name, pem_header, data, len,
                                    &tmp_handler_ctx, &try_matchcount,
                                    ui_method, ui_data);

            if (try_matchcount > 0) {

                matching_handlers[*matchcount] = handler;

                if (handler_ctx)
                    handler->destroy_ctx(&handler_ctx);
                handler_ctx = tmp_handler_ctx;

                if ((*matchcount += try_matchcount) > 1) {
                    /* more than one match => ambiguous, kill any result */
                    OSSL_STORE_INFO_free(result);
                    OSSL_STORE_INFO_free(tmp_result);
                    if (handler->destroy_ctx != NULL)
                        handler->destroy_ctx(&handler_ctx);
                    handler_ctx = NULL;
                    tmp_result = NULL;
                    result = NULL;
                }
                if (result == NULL)
                    result = tmp_result;
            }
        }

        if (*matchcount == 1 && matching_handlers[0]->repeatable) {
            ctx->_.file.last_handler = matching_handlers[0];
            ctx->_.file.last_handler_ctx = handler_ctx;
        }

        OPENSSL_free(matching_handlers);
    }

 err:
    OPENSSL_free(new_pem_name);
    BUF_MEM_free(new_mem);

    if (result != NULL
        && (t = OSSL_STORE_INFO_get_type(result)) == OSSL_STORE_INFO_EMBEDDED) {
        pem_name = new_pem_name =
            ossl_store_info_get0_EMBEDDED_pem_name(result);
        new_mem = ossl_store_info_get0_EMBEDDED_buffer(result);
        data = (unsigned char *)new_mem->data;
        len = new_mem->length;
        OPENSSL_free(result);
        result = NULL;
        goto again;
    }

    if (result != NULL)
        ERR_clear_error();

    return result;
}

static OSSL_STORE_INFO *file_load_try_repeat(OSSL_STORE_LOADER_CTX *ctx,
                                             const UI_METHOD *ui_method,
                                             void *ui_data)
{
    OSSL_STORE_INFO *result = NULL;
    int try_matchcount = 0;

    if (ctx->_.file.last_handler != NULL) {
        result =
            ctx->_.file.last_handler->try_decode(NULL, NULL, NULL, 0,
                                                 &ctx->_.file.last_handler_ctx,
                                                 &try_matchcount,
                                                 ui_method, ui_data);

        if (result == NULL) {
            ctx->_.file.last_handler->destroy_ctx(&ctx->_.file.last_handler_ctx);
            ctx->_.file.last_handler_ctx = NULL;
            ctx->_.file.last_handler = NULL;
        }
    }
    return result;
}

static void pem_free_flag(void *pem_data, int secure, size_t num)
{
    if (secure)
        OPENSSL_secure_clear_free(pem_data, num);
    else
        OPENSSL_free(pem_data);
}
static int file_read_pem(BIO *bp, char **pem_name, char **pem_header,
                         unsigned char **data, long *len,
                         const UI_METHOD *ui_method,
                         void *ui_data, int secure)
{
    int i = secure
        ? PEM_read_bio_ex(bp, pem_name, pem_header, data, len,
                          PEM_FLAG_SECURE | PEM_FLAG_EAY_COMPATIBLE)
        : PEM_read_bio(bp, pem_name, pem_header, data, len);

    if (i <= 0)
        return 0;

    /*
     * 10 is the number of characters in "Proc-Type:", which
     * PEM_get_EVP_CIPHER_INFO() requires to be present.
     * If the PEM header has less characters than that, it's
     * not worth spending cycles on it.
     */
    if (strlen(*pem_header) > 10) {
        EVP_CIPHER_INFO cipher;
        struct pem_pass_data pass_data;

        if (!PEM_get_EVP_CIPHER_INFO(*pem_header, &cipher)
            || !file_fill_pem_pass_data(&pass_data, "PEM", ui_method, ui_data)
            || !PEM_do_header(&cipher, *data, len, file_get_pem_pass,
                              &pass_data)) {
            return 0;
        }
    }
    return 1;
}

static int file_read_asn1(BIO *bp, unsigned char **data, long *len)
{
    BUF_MEM *mem = NULL;

    if (asn1_d2i_read_bio(bp, &mem) < 0)
        return 0;

    *data = (unsigned char *)mem->data;
    *len = (long)mem->length;
    OPENSSL_free(mem);

    return 1;
}

static int ends_with_dirsep(const char *uri)
{
    if (*uri != '\0')
        uri += strlen(uri) - 1;
#if defined __VMS
    if (*uri == ']' || *uri == '>' || *uri == ':')
        return 1;
#elif defined _WIN32
    if (*uri == '\\')
        return 1;
#endif
    return *uri == '/';
}

static int file_name_to_uri(OSSL_STORE_LOADER_CTX *ctx, const char *name,
                            char **data)
{
    assert(name != NULL);
    assert(data != NULL);
    {
        const char *pathsep = ends_with_dirsep(ctx->_.dir.uri) ? "" : "/";
        long calculated_length = strlen(ctx->_.dir.uri) + strlen(pathsep)
            + strlen(name) + 1 /* \0 */;

        *data = OPENSSL_zalloc(calculated_length);
        if (*data == NULL) {
            OSSL_STOREerr(OSSL_STORE_F_FILE_NAME_TO_URI, ERR_R_MALLOC_FAILURE);
            return 0;
        }

        OPENSSL_strlcat(*data, ctx->_.dir.uri, calculated_length);
        OPENSSL_strlcat(*data, pathsep, calculated_length);
        OPENSSL_strlcat(*data, name, calculated_length);
    }
    return 1;
}

static int file_name_check(OSSL_STORE_LOADER_CTX *ctx, const char *name)
{
    const char *p = NULL;

    /* If there are no search criteria, all names are accepted */
    if (ctx->_.dir.search_name[0] == '\0')
        return 1;

    /* If the expected type isn't supported, no name is accepted */
    if (ctx->expected_type != 0
        && ctx->expected_type != OSSL_STORE_INFO_CERT
        && ctx->expected_type != OSSL_STORE_INFO_CRL)
        return 0;

    /*
     * First, check the basename
     */
    if (strncasecmp(name, ctx->_.dir.search_name,
                    sizeof(ctx->_.dir.search_name) - 1) != 0
        || name[sizeof(ctx->_.dir.search_name) - 1] != '.')
        return 0;
    p = &name[sizeof(ctx->_.dir.search_name)];

    /*
     * Then, if the expected type is a CRL, check that the extension starts
     * with 'r'
     */
    if (*p == 'r') {
        p++;
        if (ctx->expected_type != 0
            && ctx->expected_type != OSSL_STORE_INFO_CRL)
            return 0;
    } else if (ctx->expected_type == OSSL_STORE_INFO_CRL) {
        return 0;
    }

    /*
     * Last, check that the rest of the extension is a decimal number, at
     * least one digit long.
     */
    if (!ossl_isdigit(*p))
        return 0;
    while (ossl_isdigit(*p))
        p++;

# ifdef __VMS
    /*
     * One extra step here, check for a possible generation number.
     */
    if (*p == ';')
        for (p++; *p != '\0'; p++)
            if (!ossl_isdigit(*p))
                break;
# endif

    /*
     * If we've reached the end of the string at this point, we've successfully
     * found a fitting file name.
     */
    return *p == '\0';
}

static int file_eof(OSSL_STORE_LOADER_CTX *ctx);
static int file_error(OSSL_STORE_LOADER_CTX *ctx);
static OSSL_STORE_INFO *file_load(OSSL_STORE_LOADER_CTX *ctx,
                                  const UI_METHOD *ui_method, void *ui_data)
{
    OSSL_STORE_INFO *result = NULL;

    ctx->errcnt = 0;
    ERR_clear_error();

    if (ctx->type == is_dir) {
        do {
            char *newname = NULL;

            if (ctx->_.dir.last_entry == NULL) {
                if (!ctx->_.dir.end_reached) {
                    char errbuf[256];
                    assert(ctx->_.dir.last_errno != 0);
                    errno = ctx->_.dir.last_errno;
                    ctx->errcnt++;
                    openssl_strerror_r(errno, errbuf, sizeof(errbuf));
                    OSSL_STOREerr(OSSL_STORE_F_FILE_LOAD, ERR_R_SYS_LIB);
                    ERR_add_error_data(1, errbuf);
                }
                return NULL;
            }

            if (ctx->_.dir.last_entry[0] != '.'
                && file_name_check(ctx, ctx->_.dir.last_entry)
                && !file_name_to_uri(ctx, ctx->_.dir.last_entry, &newname))
                return NULL;

            /*
             * On the first call (with a NULL context), OPENSSL_DIR_read()
             * cares about the second argument.  On the following calls, it
             * only cares that it isn't NULL.  Therefore, we can safely give
             * it our URI here.
             */
            ctx->_.dir.last_entry = OPENSSL_DIR_read(&ctx->_.dir.ctx,
                                                     ctx->_.dir.uri);
            ctx->_.dir.last_errno = errno;
            if (ctx->_.dir.last_entry == NULL && ctx->_.dir.last_errno == 0)
                ctx->_.dir.end_reached = 1;

            if (newname != NULL
                && (result = OSSL_STORE_INFO_new_NAME(newname)) == NULL) {
                OPENSSL_free(newname);
                OSSL_STOREerr(OSSL_STORE_F_FILE_LOAD, ERR_R_OSSL_STORE_LIB);
                return NULL;
            }
        } while (result == NULL && !file_eof(ctx));
    } else {
        int matchcount = -1;

     again:
        result = file_load_try_repeat(ctx, ui_method, ui_data);
        if (result != NULL)
            return result;

        if (file_eof(ctx))
            return NULL;

        do {
            char *pem_name = NULL;      /* PEM record name */
            char *pem_header = NULL;    /* PEM record header */
            unsigned char *data = NULL; /* DER encoded data */
            long len = 0;               /* DER encoded data length */

            matchcount = -1;
            if (ctx->type == is_pem) {
                if (!file_read_pem(ctx->_.file.file, &pem_name, &pem_header,
                                   &data, &len, ui_method, ui_data,
                                   (ctx->flags & FILE_FLAG_SECMEM) != 0)) {
                    ctx->errcnt++;
                    goto endloop;
                }
            } else {
                if (!file_read_asn1(ctx->_.file.file, &data, &len)) {
                    ctx->errcnt++;
                    goto endloop;
                }
            }

            result = file_load_try_decode(ctx, pem_name, pem_header, data, len,
                                          ui_method, ui_data, &matchcount);

            if (result != NULL)
                goto endloop;

            /*
             * If a PEM name matches more than one handler, the handlers are
             * badly coded.
             */
            if (!ossl_assert(pem_name == NULL || matchcount <= 1)) {
                ctx->errcnt++;
                goto endloop;
            }

            if (matchcount > 1) {
                OSSL_STOREerr(OSSL_STORE_F_FILE_LOAD,
                              OSSL_STORE_R_AMBIGUOUS_CONTENT_TYPE);
            } else if (matchcount == 1) {
                /*
                 * If there are other errors on the stack, they already show
                 * what the problem is.
                 */
                if (ERR_peek_error() == 0) {
                    OSSL_STOREerr(OSSL_STORE_F_FILE_LOAD,
                                  OSSL_STORE_R_UNSUPPORTED_CONTENT_TYPE);
                    if (pem_name != NULL)
                        ERR_add_error_data(3, "PEM type is '", pem_name, "'");
                }
            }
            if (matchcount > 0)
                ctx->errcnt++;

         endloop:
            pem_free_flag(pem_name, (ctx->flags & FILE_FLAG_SECMEM) != 0, 0);
            pem_free_flag(pem_header, (ctx->flags & FILE_FLAG_SECMEM) != 0, 0);
            pem_free_flag(data, (ctx->flags & FILE_FLAG_SECMEM) != 0, len);
        } while (matchcount == 0 && !file_eof(ctx) && !file_error(ctx));

        /* We bail out on ambiguity */
        if (matchcount > 1)
            return NULL;

        if (result != NULL
            && ctx->expected_type != 0
            && ctx->expected_type != OSSL_STORE_INFO_get_type(result)) {
            OSSL_STORE_INFO_free(result);
            goto again;
        }
    }

    return result;
}

static int file_error(OSSL_STORE_LOADER_CTX *ctx)
{
    return ctx->errcnt > 0;
}

static int file_eof(OSSL_STORE_LOADER_CTX *ctx)
{
    if (ctx->type == is_dir)
        return ctx->_.dir.end_reached;

    if (ctx->_.file.last_handler != NULL
        && !ctx->_.file.last_handler->eof(ctx->_.file.last_handler_ctx))
        return 0;
    return BIO_eof(ctx->_.file.file);
}

static int file_close(OSSL_STORE_LOADER_CTX *ctx)
{
    if (ctx->type == is_dir) {
        OPENSSL_DIR_end(&ctx->_.dir.ctx);
    } else {
        BIO_free_all(ctx->_.file.file);
    }
    OSSL_STORE_LOADER_CTX_free(ctx);
    return 1;
}

int ossl_store_file_detach_pem_bio_int(OSSL_STORE_LOADER_CTX *ctx)
{
    OSSL_STORE_LOADER_CTX_free(ctx);
    return 1;
}

static OSSL_STORE_LOADER file_loader =
    {
        "file",
        NULL,
        file_open,
        file_ctrl,
        file_expect,
        file_find,
        file_load,
        file_eof,
        file_error,
        file_close
    };

static void store_file_loader_deinit(void)
{
    ossl_store_unregister_loader_int(file_loader.scheme);
}

int ossl_store_file_loader_init(void)
{
    int ret = ossl_store_register_loader_int(&file_loader);

    OPENSSL_atexit(store_file_loader_deinit);
    return ret;
}
