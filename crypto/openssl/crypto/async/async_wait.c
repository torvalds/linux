/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* This must be the first #include file */
#include "async_locl.h"

#include <openssl/err.h>

ASYNC_WAIT_CTX *ASYNC_WAIT_CTX_new(void)
{
    return OPENSSL_zalloc(sizeof(ASYNC_WAIT_CTX));
}

void ASYNC_WAIT_CTX_free(ASYNC_WAIT_CTX *ctx)
{
    struct fd_lookup_st *curr;
    struct fd_lookup_st *next;

    if (ctx == NULL)
        return;

    curr = ctx->fds;
    while (curr != NULL) {
        if (!curr->del) {
            /* Only try and cleanup if it hasn't been marked deleted */
            if (curr->cleanup != NULL)
                curr->cleanup(ctx, curr->key, curr->fd, curr->custom_data);
        }
        /* Always free the fd_lookup_st */
        next = curr->next;
        OPENSSL_free(curr);
        curr = next;
    }

    OPENSSL_free(ctx);
}
int ASYNC_WAIT_CTX_set_wait_fd(ASYNC_WAIT_CTX *ctx, const void *key,
                               OSSL_ASYNC_FD fd, void *custom_data,
                               void (*cleanup)(ASYNC_WAIT_CTX *, const void *,
                                               OSSL_ASYNC_FD, void *))
{
    struct fd_lookup_st *fdlookup;

    if ((fdlookup = OPENSSL_zalloc(sizeof(*fdlookup))) == NULL) {
        ASYNCerr(ASYNC_F_ASYNC_WAIT_CTX_SET_WAIT_FD, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    fdlookup->key = key;
    fdlookup->fd = fd;
    fdlookup->custom_data = custom_data;
    fdlookup->cleanup = cleanup;
    fdlookup->add = 1;
    fdlookup->next = ctx->fds;
    ctx->fds = fdlookup;
    ctx->numadd++;
    return 1;
}

int ASYNC_WAIT_CTX_get_fd(ASYNC_WAIT_CTX *ctx, const void *key,
                          OSSL_ASYNC_FD *fd, void **custom_data)
{
    struct fd_lookup_st *curr;

    curr = ctx->fds;
    while (curr != NULL) {
        if (curr->del) {
            /* This one has been marked deleted so do nothing */
            curr = curr->next;
            continue;
        }
        if (curr->key == key) {
            *fd = curr->fd;
            *custom_data = curr->custom_data;
            return 1;
        }
        curr = curr->next;
    }
    return 0;
}

int ASYNC_WAIT_CTX_get_all_fds(ASYNC_WAIT_CTX *ctx, OSSL_ASYNC_FD *fd,
                               size_t *numfds)
{
    struct fd_lookup_st *curr;

    curr = ctx->fds;
    *numfds = 0;
    while (curr != NULL) {
        if (curr->del) {
            /* This one has been marked deleted so do nothing */
            curr = curr->next;
            continue;
        }
        if (fd != NULL) {
            *fd = curr->fd;
            fd++;
        }
        (*numfds)++;
        curr = curr->next;
    }
    return 1;
}

int ASYNC_WAIT_CTX_get_changed_fds(ASYNC_WAIT_CTX *ctx, OSSL_ASYNC_FD *addfd,
                                   size_t *numaddfds, OSSL_ASYNC_FD *delfd,
                                   size_t *numdelfds)
{
    struct fd_lookup_st *curr;

    *numaddfds = ctx->numadd;
    *numdelfds = ctx->numdel;
    if (addfd == NULL && delfd == NULL)
        return 1;

    curr = ctx->fds;

    while (curr != NULL) {
        /* We ignore fds that have been marked as both added and deleted */
        if (curr->del && !curr->add && (delfd != NULL)) {
            *delfd = curr->fd;
            delfd++;
        }
        if (curr->add && !curr->del && (addfd != NULL)) {
            *addfd = curr->fd;
            addfd++;
        }
        curr = curr->next;
    }

    return 1;
}

int ASYNC_WAIT_CTX_clear_fd(ASYNC_WAIT_CTX *ctx, const void *key)
{
    struct fd_lookup_st *curr, *prev;

    curr = ctx->fds;
    prev = NULL;
    while (curr != NULL) {
        if (curr->del == 1) {
            /* This one has been marked deleted already so do nothing */
            prev = curr;
            curr = curr->next;
            continue;
        }
        if (curr->key == key) {
            /* If fd has just been added, remove it from the list */
            if (curr->add == 1) {
                if (ctx->fds == curr) {
                    ctx->fds = curr->next;
                } else {
                    prev->next = curr->next;
                }

                /* It is responsibility of the caller to cleanup before calling
                 * ASYNC_WAIT_CTX_clear_fd
                 */
                OPENSSL_free(curr);
                ctx->numadd--;
                return 1;
            }

            /*
             * Mark it as deleted. We don't call cleanup if explicitly asked
             * to clear an fd. We assume the caller is going to do that (if
             * appropriate).
             */
            curr->del = 1;
            ctx->numdel++;
            return 1;
        }
        prev = curr;
        curr = curr->next;
    }
    return 0;
}

void async_wait_ctx_reset_counts(ASYNC_WAIT_CTX *ctx)
{
    struct fd_lookup_st *curr, *prev = NULL;

    ctx->numadd = 0;
    ctx->numdel = 0;

    curr = ctx->fds;

    while (curr != NULL) {
        if (curr->del) {
            if (prev == NULL)
                ctx->fds = curr->next;
            else
                prev->next = curr->next;
            OPENSSL_free(curr);
            if (prev == NULL)
                curr = ctx->fds;
            else
                curr = prev->next;
            continue;
        }
        if (curr->add) {
            curr->add = 0;
        }
        prev = curr;
        curr = curr->next;
    }
}
