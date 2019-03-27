/*
 * Copyright 2001-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "e_os.h"
#include "eng_int.h"

/*
 * Initialise a engine type for use (or up its functional reference count if
 * it's already in use). This version is only used internally.
 */
int engine_unlocked_init(ENGINE *e)
{
    int to_return = 1;

    if ((e->funct_ref == 0) && e->init)
        /*
         * This is the first functional reference and the engine requires
         * initialisation so we do it now.
         */
        to_return = e->init(e);
    if (to_return) {
        /*
         * OK, we return a functional reference which is also a structural
         * reference.
         */
        e->struct_ref++;
        e->funct_ref++;
        engine_ref_debug(e, 0, 1);
        engine_ref_debug(e, 1, 1);
    }
    return to_return;
}

/*
 * Free a functional reference to a engine type. This version is only used
 * internally.
 */
int engine_unlocked_finish(ENGINE *e, int unlock_for_handlers)
{
    int to_return = 1;

    /*
     * Reduce the functional reference count here so if it's the terminating
     * case, we can release the lock safely and call the finish() handler
     * without risk of a race. We get a race if we leave the count until
     * after and something else is calling "finish" at the same time -
     * there's a chance that both threads will together take the count from 2
     * to 0 without either calling finish().
     */
    e->funct_ref--;
    engine_ref_debug(e, 1, -1);
    if ((e->funct_ref == 0) && e->finish) {
        if (unlock_for_handlers)
            CRYPTO_THREAD_unlock(global_engine_lock);
        to_return = e->finish(e);
        if (unlock_for_handlers)
            CRYPTO_THREAD_write_lock(global_engine_lock);
        if (!to_return)
            return 0;
    }
    REF_ASSERT_ISNT(e->funct_ref < 0);
    /* Release the structural reference too */
    if (!engine_free_util(e, 0)) {
        ENGINEerr(ENGINE_F_ENGINE_UNLOCKED_FINISH, ENGINE_R_FINISH_FAILED);
        return 0;
    }
    return to_return;
}

/* The API (locked) version of "init" */
int ENGINE_init(ENGINE *e)
{
    int ret;
    if (e == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_INIT, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    if (!RUN_ONCE(&engine_lock_init, do_engine_lock_init)) {
        ENGINEerr(ENGINE_F_ENGINE_INIT, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    CRYPTO_THREAD_write_lock(global_engine_lock);
    ret = engine_unlocked_init(e);
    CRYPTO_THREAD_unlock(global_engine_lock);
    return ret;
}

/* The API (locked) version of "finish" */
int ENGINE_finish(ENGINE *e)
{
    int to_return = 1;

    if (e == NULL)
        return 1;
    CRYPTO_THREAD_write_lock(global_engine_lock);
    to_return = engine_unlocked_finish(e, 1);
    CRYPTO_THREAD_unlock(global_engine_lock);
    if (!to_return) {
        ENGINEerr(ENGINE_F_ENGINE_FINISH, ENGINE_R_FINISH_FAILED);
        return 0;
    }
    return to_return;
}
