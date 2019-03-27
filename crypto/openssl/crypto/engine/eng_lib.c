/*
 * Copyright 2001-2019 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "e_os.h"
#include "eng_int.h"
#include <openssl/rand.h>
#include "internal/refcount.h"

CRYPTO_RWLOCK *global_engine_lock;

CRYPTO_ONCE engine_lock_init = CRYPTO_ONCE_STATIC_INIT;

/* The "new"/"free" stuff first */

DEFINE_RUN_ONCE(do_engine_lock_init)
{
    if (!OPENSSL_init_crypto(0, NULL))
        return 0;
    global_engine_lock = CRYPTO_THREAD_lock_new();
    return global_engine_lock != NULL;
}

ENGINE *ENGINE_new(void)
{
    ENGINE *ret;

    if (!RUN_ONCE(&engine_lock_init, do_engine_lock_init)
        || (ret = OPENSSL_zalloc(sizeof(*ret))) == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_NEW, ERR_R_MALLOC_FAILURE);
        return NULL;
    }
    ret->struct_ref = 1;
    engine_ref_debug(ret, 0, 1);
    if (!CRYPTO_new_ex_data(CRYPTO_EX_INDEX_ENGINE, ret, &ret->ex_data)) {
        OPENSSL_free(ret);
        return NULL;
    }
    return ret;
}

/*
 * Placed here (close proximity to ENGINE_new) so that modifications to the
 * elements of the ENGINE structure are more likely to be caught and changed
 * here.
 */
void engine_set_all_null(ENGINE *e)
{
    e->id = NULL;
    e->name = NULL;
    e->rsa_meth = NULL;
    e->dsa_meth = NULL;
    e->dh_meth = NULL;
    e->rand_meth = NULL;
    e->ciphers = NULL;
    e->digests = NULL;
    e->destroy = NULL;
    e->init = NULL;
    e->finish = NULL;
    e->ctrl = NULL;
    e->load_privkey = NULL;
    e->load_pubkey = NULL;
    e->cmd_defns = NULL;
    e->flags = 0;
}

int engine_free_util(ENGINE *e, int not_locked)
{
    int i;

    if (e == NULL)
        return 1;
    if (not_locked)
        CRYPTO_DOWN_REF(&e->struct_ref, &i, global_engine_lock);
    else
        i = --e->struct_ref;
    engine_ref_debug(e, 0, -1);
    if (i > 0)
        return 1;
    REF_ASSERT_ISNT(i < 0);
    /* Free up any dynamically allocated public key methods */
    engine_pkey_meths_free(e);
    engine_pkey_asn1_meths_free(e);
    /*
     * Give the ENGINE a chance to do any structural cleanup corresponding to
     * allocation it did in its constructor (eg. unload error strings)
     */
    if (e->destroy)
        e->destroy(e);
    CRYPTO_free_ex_data(CRYPTO_EX_INDEX_ENGINE, e, &e->ex_data);
    OPENSSL_free(e);
    return 1;
}

int ENGINE_free(ENGINE *e)
{
    return engine_free_util(e, 1);
}

/* Cleanup stuff */

/*
 * engine_cleanup_int() is coded such that anything that does work that will
 * need cleanup can register a "cleanup" callback here. That way we don't get
 * linker bloat by referring to all *possible* cleanups, but any linker bloat
 * into code "X" will cause X's cleanup function to end up here.
 */
static STACK_OF(ENGINE_CLEANUP_ITEM) *cleanup_stack = NULL;
static int int_cleanup_check(int create)
{
    if (cleanup_stack)
        return 1;
    if (!create)
        return 0;
    cleanup_stack = sk_ENGINE_CLEANUP_ITEM_new_null();
    return (cleanup_stack ? 1 : 0);
}

static ENGINE_CLEANUP_ITEM *int_cleanup_item(ENGINE_CLEANUP_CB *cb)
{
    ENGINE_CLEANUP_ITEM *item;

    if ((item = OPENSSL_malloc(sizeof(*item))) == NULL) {
        ENGINEerr(ENGINE_F_INT_CLEANUP_ITEM, ERR_R_MALLOC_FAILURE);
        return NULL;
    }
    item->cb = cb;
    return item;
}

void engine_cleanup_add_first(ENGINE_CLEANUP_CB *cb)
{
    ENGINE_CLEANUP_ITEM *item;

    if (!int_cleanup_check(1))
        return;
    item = int_cleanup_item(cb);
    if (item)
        sk_ENGINE_CLEANUP_ITEM_insert(cleanup_stack, item, 0);
}

void engine_cleanup_add_last(ENGINE_CLEANUP_CB *cb)
{
    ENGINE_CLEANUP_ITEM *item;
    if (!int_cleanup_check(1))
        return;
    item = int_cleanup_item(cb);
    if (item != NULL) {
        if (sk_ENGINE_CLEANUP_ITEM_push(cleanup_stack, item) <= 0)
            OPENSSL_free(item);
    }
}

/* The API function that performs all cleanup */
static void engine_cleanup_cb_free(ENGINE_CLEANUP_ITEM *item)
{
    (*(item->cb)) ();
    OPENSSL_free(item);
}

void engine_cleanup_int(void)
{
    if (int_cleanup_check(0)) {
        sk_ENGINE_CLEANUP_ITEM_pop_free(cleanup_stack,
                                        engine_cleanup_cb_free);
        cleanup_stack = NULL;
    }
    CRYPTO_THREAD_lock_free(global_engine_lock);
}

/* Now the "ex_data" support */

int ENGINE_set_ex_data(ENGINE *e, int idx, void *arg)
{
    return CRYPTO_set_ex_data(&e->ex_data, idx, arg);
}

void *ENGINE_get_ex_data(const ENGINE *e, int idx)
{
    return CRYPTO_get_ex_data(&e->ex_data, idx);
}

/*
 * Functions to get/set an ENGINE's elements - mainly to avoid exposing the
 * ENGINE structure itself.
 */

int ENGINE_set_id(ENGINE *e, const char *id)
{
    if (id == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_SET_ID, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    e->id = id;
    return 1;
}

int ENGINE_set_name(ENGINE *e, const char *name)
{
    if (name == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_SET_NAME, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    e->name = name;
    return 1;
}

int ENGINE_set_destroy_function(ENGINE *e, ENGINE_GEN_INT_FUNC_PTR destroy_f)
{
    e->destroy = destroy_f;
    return 1;
}

int ENGINE_set_init_function(ENGINE *e, ENGINE_GEN_INT_FUNC_PTR init_f)
{
    e->init = init_f;
    return 1;
}

int ENGINE_set_finish_function(ENGINE *e, ENGINE_GEN_INT_FUNC_PTR finish_f)
{
    e->finish = finish_f;
    return 1;
}

int ENGINE_set_ctrl_function(ENGINE *e, ENGINE_CTRL_FUNC_PTR ctrl_f)
{
    e->ctrl = ctrl_f;
    return 1;
}

int ENGINE_set_flags(ENGINE *e, int flags)
{
    e->flags = flags;
    return 1;
}

int ENGINE_set_cmd_defns(ENGINE *e, const ENGINE_CMD_DEFN *defns)
{
    e->cmd_defns = defns;
    return 1;
}

const char *ENGINE_get_id(const ENGINE *e)
{
    return e->id;
}

const char *ENGINE_get_name(const ENGINE *e)
{
    return e->name;
}

ENGINE_GEN_INT_FUNC_PTR ENGINE_get_destroy_function(const ENGINE *e)
{
    return e->destroy;
}

ENGINE_GEN_INT_FUNC_PTR ENGINE_get_init_function(const ENGINE *e)
{
    return e->init;
}

ENGINE_GEN_INT_FUNC_PTR ENGINE_get_finish_function(const ENGINE *e)
{
    return e->finish;
}

ENGINE_CTRL_FUNC_PTR ENGINE_get_ctrl_function(const ENGINE *e)
{
    return e->ctrl;
}

int ENGINE_get_flags(const ENGINE *e)
{
    return e->flags;
}

const ENGINE_CMD_DEFN *ENGINE_get_cmd_defns(const ENGINE *e)
{
    return e->cmd_defns;
}

/*
 * eng_lib.o is pretty much linked into anything that touches ENGINE already,
 * so put the "static_state" hack here.
 */

static int internal_static_hack = 0;

void *ENGINE_get_static_state(void)
{
    return &internal_static_hack;
}
