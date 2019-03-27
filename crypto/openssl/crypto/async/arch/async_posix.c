/*
 * Copyright 2015-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* This must be the first #include file */
#include "../async_locl.h"

#ifdef ASYNC_POSIX

# include <stddef.h>
# include <unistd.h>

#define STACKSIZE       32768

int ASYNC_is_capable(void)
{
    ucontext_t ctx;

    /*
     * Some platforms provide getcontext() but it does not work (notably
     * MacOSX PPC64). Check for a working getcontext();
     */
    return getcontext(&ctx) == 0;
}

void async_local_cleanup(void)
{
}

int async_fibre_makecontext(async_fibre *fibre)
{
    fibre->env_init = 0;
    if (getcontext(&fibre->fibre) == 0) {
        fibre->fibre.uc_stack.ss_sp = OPENSSL_malloc(STACKSIZE);
        if (fibre->fibre.uc_stack.ss_sp != NULL) {
            fibre->fibre.uc_stack.ss_size = STACKSIZE;
            fibre->fibre.uc_link = NULL;
            makecontext(&fibre->fibre, async_start_func, 0);
            return 1;
        }
    } else {
        fibre->fibre.uc_stack.ss_sp = NULL;
    }
    return 0;
}

void async_fibre_free(async_fibre *fibre)
{
    OPENSSL_free(fibre->fibre.uc_stack.ss_sp);
    fibre->fibre.uc_stack.ss_sp = NULL;
}

#endif
