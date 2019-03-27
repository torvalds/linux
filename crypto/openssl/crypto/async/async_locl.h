/*
 * Copyright 2015-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * Must do this before including any header files, because on MacOS/X <stlib.h>
 * includes <signal.h> which includes <ucontext.h>
 */
#if defined(__APPLE__) && defined(__MACH__) && !defined(_XOPEN_SOURCE)
# define _XOPEN_SOURCE          /* Otherwise incomplete ucontext_t structure */
# pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#if defined(_WIN32)
# include <windows.h>
#endif

#include "internal/async.h"
#include <openssl/crypto.h>

typedef struct async_ctx_st async_ctx;
typedef struct async_pool_st async_pool;

#include "arch/async_win.h"
#include "arch/async_posix.h"
#include "arch/async_null.h"

struct async_ctx_st {
    async_fibre dispatcher;
    ASYNC_JOB *currjob;
    unsigned int blocked;
};

struct async_job_st {
    async_fibre fibrectx;
    int (*func) (void *);
    void *funcargs;
    int ret;
    int status;
    ASYNC_WAIT_CTX *waitctx;
};

struct fd_lookup_st {
    const void *key;
    OSSL_ASYNC_FD fd;
    void *custom_data;
    void (*cleanup)(ASYNC_WAIT_CTX *, const void *, OSSL_ASYNC_FD, void *);
    int add;
    int del;
    struct fd_lookup_st *next;
};

struct async_wait_ctx_st {
    struct fd_lookup_st *fds;
    size_t numadd;
    size_t numdel;
};

DEFINE_STACK_OF(ASYNC_JOB)

struct async_pool_st {
    STACK_OF(ASYNC_JOB) *jobs;
    size_t curr_size;
    size_t max_size;
};

void async_local_cleanup(void);
void async_start_func(void);
async_ctx *async_get_ctx(void);

void async_wait_ctx_reset_counts(ASYNC_WAIT_CTX *ctx);

