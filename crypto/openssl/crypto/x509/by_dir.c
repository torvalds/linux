/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "e_os.h"
#include "internal/cryptlib.h"
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>

#ifndef OPENSSL_NO_POSIX_IO
# include <sys/stat.h>
#endif

#include <openssl/x509.h>
#include "internal/x509_int.h"
#include "x509_lcl.h"

struct lookup_dir_hashes_st {
    unsigned long hash;
    int suffix;
};

struct lookup_dir_entry_st {
    char *dir;
    int dir_type;
    STACK_OF(BY_DIR_HASH) *hashes;
};

typedef struct lookup_dir_st {
    BUF_MEM *buffer;
    STACK_OF(BY_DIR_ENTRY) *dirs;
    CRYPTO_RWLOCK *lock;
} BY_DIR;

static int dir_ctrl(X509_LOOKUP *ctx, int cmd, const char *argp, long argl,
                    char **ret);
static int new_dir(X509_LOOKUP *lu);
static void free_dir(X509_LOOKUP *lu);
static int add_cert_dir(BY_DIR *ctx, const char *dir, int type);
static int get_cert_by_subject(X509_LOOKUP *xl, X509_LOOKUP_TYPE type,
                               X509_NAME *name, X509_OBJECT *ret);
static X509_LOOKUP_METHOD x509_dir_lookup = {
    "Load certs from files in a directory",
    new_dir,                    /* new_item */
    free_dir,                   /* free */
    NULL,                       /* init */
    NULL,                       /* shutdown */
    dir_ctrl,                   /* ctrl */
    get_cert_by_subject,        /* get_by_subject */
    NULL,                       /* get_by_issuer_serial */
    NULL,                       /* get_by_fingerprint */
    NULL,                       /* get_by_alias */
};

X509_LOOKUP_METHOD *X509_LOOKUP_hash_dir(void)
{
    return &x509_dir_lookup;
}

static int dir_ctrl(X509_LOOKUP *ctx, int cmd, const char *argp, long argl,
                    char **retp)
{
    int ret = 0;
    BY_DIR *ld = (BY_DIR *)ctx->method_data;

    switch (cmd) {
    case X509_L_ADD_DIR:
        if (argl == X509_FILETYPE_DEFAULT) {
            const char *dir = ossl_safe_getenv(X509_get_default_cert_dir_env());

            if (dir)
                ret = add_cert_dir(ld, dir, X509_FILETYPE_PEM);
            else
                ret = add_cert_dir(ld, X509_get_default_cert_dir(),
                                   X509_FILETYPE_PEM);
            if (!ret) {
                X509err(X509_F_DIR_CTRL, X509_R_LOADING_CERT_DIR);
            }
        } else
            ret = add_cert_dir(ld, argp, (int)argl);
        break;
    }
    return ret;
}

static int new_dir(X509_LOOKUP *lu)
{
    BY_DIR *a = OPENSSL_malloc(sizeof(*a));

    if (a == NULL) {
        X509err(X509_F_NEW_DIR, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    if ((a->buffer = BUF_MEM_new()) == NULL) {
        X509err(X509_F_NEW_DIR, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    a->dirs = NULL;
    a->lock = CRYPTO_THREAD_lock_new();
    if (a->lock == NULL) {
        BUF_MEM_free(a->buffer);
        X509err(X509_F_NEW_DIR, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    lu->method_data = a;
    return 1;

 err:
    OPENSSL_free(a);
    return 0;
}

static void by_dir_hash_free(BY_DIR_HASH *hash)
{
    OPENSSL_free(hash);
}

static int by_dir_hash_cmp(const BY_DIR_HASH *const *a,
                           const BY_DIR_HASH *const *b)
{
    if ((*a)->hash > (*b)->hash)
        return 1;
    if ((*a)->hash < (*b)->hash)
        return -1;
    return 0;
}

static void by_dir_entry_free(BY_DIR_ENTRY *ent)
{
    OPENSSL_free(ent->dir);
    sk_BY_DIR_HASH_pop_free(ent->hashes, by_dir_hash_free);
    OPENSSL_free(ent);
}

static void free_dir(X509_LOOKUP *lu)
{
    BY_DIR *a = (BY_DIR *)lu->method_data;

    sk_BY_DIR_ENTRY_pop_free(a->dirs, by_dir_entry_free);
    BUF_MEM_free(a->buffer);
    CRYPTO_THREAD_lock_free(a->lock);
    OPENSSL_free(a);
}

static int add_cert_dir(BY_DIR *ctx, const char *dir, int type)
{
    int j;
    size_t len;
    const char *s, *ss, *p;

    if (dir == NULL || !*dir) {
        X509err(X509_F_ADD_CERT_DIR, X509_R_INVALID_DIRECTORY);
        return 0;
    }

    s = dir;
    p = s;
    do {
        if ((*p == LIST_SEPARATOR_CHAR) || (*p == '\0')) {
            BY_DIR_ENTRY *ent;

            ss = s;
            s = p + 1;
            len = p - ss;
            if (len == 0)
                continue;
            for (j = 0; j < sk_BY_DIR_ENTRY_num(ctx->dirs); j++) {
                ent = sk_BY_DIR_ENTRY_value(ctx->dirs, j);
                if (strlen(ent->dir) == len && strncmp(ent->dir, ss, len) == 0)
                    break;
            }
            if (j < sk_BY_DIR_ENTRY_num(ctx->dirs))
                continue;
            if (ctx->dirs == NULL) {
                ctx->dirs = sk_BY_DIR_ENTRY_new_null();
                if (!ctx->dirs) {
                    X509err(X509_F_ADD_CERT_DIR, ERR_R_MALLOC_FAILURE);
                    return 0;
                }
            }
            ent = OPENSSL_malloc(sizeof(*ent));
            if (ent == NULL) {
                X509err(X509_F_ADD_CERT_DIR, ERR_R_MALLOC_FAILURE);
                return 0;
            }
            ent->dir_type = type;
            ent->hashes = sk_BY_DIR_HASH_new(by_dir_hash_cmp);
            ent->dir = OPENSSL_strndup(ss, len);
            if (ent->dir == NULL || ent->hashes == NULL) {
                by_dir_entry_free(ent);
                return 0;
            }
            if (!sk_BY_DIR_ENTRY_push(ctx->dirs, ent)) {
                by_dir_entry_free(ent);
                X509err(X509_F_ADD_CERT_DIR, ERR_R_MALLOC_FAILURE);
                return 0;
            }
        }
    } while (*p++ != '\0');
    return 1;
}

static int get_cert_by_subject(X509_LOOKUP *xl, X509_LOOKUP_TYPE type,
                               X509_NAME *name, X509_OBJECT *ret)
{
    BY_DIR *ctx;
    union {
        X509 st_x509;
        X509_CRL crl;
    } data;
    int ok = 0;
    int i, j, k;
    unsigned long h;
    BUF_MEM *b = NULL;
    X509_OBJECT stmp, *tmp;
    const char *postfix = "";

    if (name == NULL)
        return 0;

    stmp.type = type;
    if (type == X509_LU_X509) {
        data.st_x509.cert_info.subject = name;
        stmp.data.x509 = &data.st_x509;
        postfix = "";
    } else if (type == X509_LU_CRL) {
        data.crl.crl.issuer = name;
        stmp.data.crl = &data.crl;
        postfix = "r";
    } else {
        X509err(X509_F_GET_CERT_BY_SUBJECT, X509_R_WRONG_LOOKUP_TYPE);
        goto finish;
    }

    if ((b = BUF_MEM_new()) == NULL) {
        X509err(X509_F_GET_CERT_BY_SUBJECT, ERR_R_BUF_LIB);
        goto finish;
    }

    ctx = (BY_DIR *)xl->method_data;

    h = X509_NAME_hash(name);
    for (i = 0; i < sk_BY_DIR_ENTRY_num(ctx->dirs); i++) {
        BY_DIR_ENTRY *ent;
        int idx;
        BY_DIR_HASH htmp, *hent;

        ent = sk_BY_DIR_ENTRY_value(ctx->dirs, i);
        j = strlen(ent->dir) + 1 + 8 + 6 + 1 + 1;
        if (!BUF_MEM_grow(b, j)) {
            X509err(X509_F_GET_CERT_BY_SUBJECT, ERR_R_MALLOC_FAILURE);
            goto finish;
        }
        if (type == X509_LU_CRL && ent->hashes) {
            htmp.hash = h;
            CRYPTO_THREAD_read_lock(ctx->lock);
            idx = sk_BY_DIR_HASH_find(ent->hashes, &htmp);
            if (idx >= 0) {
                hent = sk_BY_DIR_HASH_value(ent->hashes, idx);
                k = hent->suffix;
            } else {
                hent = NULL;
                k = 0;
            }
            CRYPTO_THREAD_unlock(ctx->lock);
        } else {
            k = 0;
            hent = NULL;
        }
        for (;;) {
            char c = '/';
#ifdef OPENSSL_SYS_VMS
            c = ent->dir[strlen(ent->dir) - 1];
            if (c != ':' && c != '>' && c != ']') {
                /*
                 * If no separator is present, we assume the directory
                 * specifier is a logical name, and add a colon.  We really
                 * should use better VMS routines for merging things like
                 * this, but this will do for now... -- Richard Levitte
                 */
                c = ':';
            } else {
                c = '\0';
            }
#endif
            if (c == '\0') {
                /*
                 * This is special.  When c == '\0', no directory separator
                 * should be added.
                 */
                BIO_snprintf(b->data, b->max,
                             "%s%08lx.%s%d", ent->dir, h, postfix, k);
            } else {
                BIO_snprintf(b->data, b->max,
                             "%s%c%08lx.%s%d", ent->dir, c, h, postfix, k);
            }
#ifndef OPENSSL_NO_POSIX_IO
# ifdef _WIN32
#  define stat _stat
# endif
            {
                struct stat st;
                if (stat(b->data, &st) < 0)
                    break;
            }
#endif
            /* found one. */
            if (type == X509_LU_X509) {
                if ((X509_load_cert_file(xl, b->data, ent->dir_type)) == 0)
                    break;
            } else if (type == X509_LU_CRL) {
                if ((X509_load_crl_file(xl, b->data, ent->dir_type)) == 0)
                    break;
            }
            /* else case will caught higher up */
            k++;
        }

        /*
         * we have added it to the cache so now pull it out again
         */
        CRYPTO_THREAD_write_lock(ctx->lock);
        j = sk_X509_OBJECT_find(xl->store_ctx->objs, &stmp);
        tmp = sk_X509_OBJECT_value(xl->store_ctx->objs, j);
        CRYPTO_THREAD_unlock(ctx->lock);

        /* If a CRL, update the last file suffix added for this */

        if (type == X509_LU_CRL) {
            CRYPTO_THREAD_write_lock(ctx->lock);
            /*
             * Look for entry again in case another thread added an entry
             * first.
             */
            if (hent == NULL) {
                htmp.hash = h;
                idx = sk_BY_DIR_HASH_find(ent->hashes, &htmp);
                hent = sk_BY_DIR_HASH_value(ent->hashes, idx);
            }
            if (hent == NULL) {
                hent = OPENSSL_malloc(sizeof(*hent));
                if (hent == NULL) {
                    CRYPTO_THREAD_unlock(ctx->lock);
                    X509err(X509_F_GET_CERT_BY_SUBJECT, ERR_R_MALLOC_FAILURE);
                    ok = 0;
                    goto finish;
                }
                hent->hash = h;
                hent->suffix = k;
                if (!sk_BY_DIR_HASH_push(ent->hashes, hent)) {
                    CRYPTO_THREAD_unlock(ctx->lock);
                    OPENSSL_free(hent);
                    X509err(X509_F_GET_CERT_BY_SUBJECT, ERR_R_MALLOC_FAILURE);
                    ok = 0;
                    goto finish;
                }
            } else if (hent->suffix < k) {
                hent->suffix = k;
            }

            CRYPTO_THREAD_unlock(ctx->lock);

        }

        if (tmp != NULL) {
            ok = 1;
            ret->type = tmp->type;
            memcpy(&ret->data, &tmp->data, sizeof(ret->data));

            /*
             * Clear any errors that might have been raised processing empty
             * or malformed files.
             */
            ERR_clear_error();

            goto finish;
        }
    }
 finish:
    BUF_MEM_free(b);
    return ok;
}
