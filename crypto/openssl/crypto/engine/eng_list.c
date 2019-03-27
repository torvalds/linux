/*
 * Copyright 2001-2018 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright (c) 2002, Oracle and/or its affiliates. All rights reserved
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "eng_int.h"

/*
 * The linked-list of pointers to engine types. engine_list_head incorporates
 * an implicit structural reference but engine_list_tail does not - the
 * latter is a computational optimization and only points to something that
 * is already pointed to by its predecessor in the list (or engine_list_head
 * itself). In the same way, the use of the "prev" pointer in each ENGINE is
 * to save excessive list iteration, it doesn't correspond to an extra
 * structural reference. Hence, engine_list_head, and each non-null "next"
 * pointer account for the list itself assuming exactly 1 structural
 * reference on each list member.
 */
static ENGINE *engine_list_head = NULL;
static ENGINE *engine_list_tail = NULL;

/*
 * This cleanup function is only needed internally. If it should be called,
 * we register it with the "engine_cleanup_int()" stack to be called during
 * cleanup.
 */

static void engine_list_cleanup(void)
{
    ENGINE *iterator = engine_list_head;

    while (iterator != NULL) {
        ENGINE_remove(iterator);
        iterator = engine_list_head;
    }
    return;
}

/*
 * These static functions starting with a lower case "engine_" always take
 * place when global_engine_lock has been locked up.
 */
static int engine_list_add(ENGINE *e)
{
    int conflict = 0;
    ENGINE *iterator = NULL;

    if (e == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_LIST_ADD, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    iterator = engine_list_head;
    while (iterator && !conflict) {
        conflict = (strcmp(iterator->id, e->id) == 0);
        iterator = iterator->next;
    }
    if (conflict) {
        ENGINEerr(ENGINE_F_ENGINE_LIST_ADD, ENGINE_R_CONFLICTING_ENGINE_ID);
        return 0;
    }
    if (engine_list_head == NULL) {
        /* We are adding to an empty list. */
        if (engine_list_tail) {
            ENGINEerr(ENGINE_F_ENGINE_LIST_ADD, ENGINE_R_INTERNAL_LIST_ERROR);
            return 0;
        }
        engine_list_head = e;
        e->prev = NULL;
        /*
         * The first time the list allocates, we should register the cleanup.
         */
        engine_cleanup_add_last(engine_list_cleanup);
    } else {
        /* We are adding to the tail of an existing list. */
        if ((engine_list_tail == NULL) || (engine_list_tail->next != NULL)) {
            ENGINEerr(ENGINE_F_ENGINE_LIST_ADD, ENGINE_R_INTERNAL_LIST_ERROR);
            return 0;
        }
        engine_list_tail->next = e;
        e->prev = engine_list_tail;
    }
    /*
     * Having the engine in the list assumes a structural reference.
     */
    e->struct_ref++;
    engine_ref_debug(e, 0, 1);
    /* However it came to be, e is the last item in the list. */
    engine_list_tail = e;
    e->next = NULL;
    return 1;
}

static int engine_list_remove(ENGINE *e)
{
    ENGINE *iterator;

    if (e == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_LIST_REMOVE, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    /* We need to check that e is in our linked list! */
    iterator = engine_list_head;
    while (iterator && (iterator != e))
        iterator = iterator->next;
    if (iterator == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_LIST_REMOVE,
                  ENGINE_R_ENGINE_IS_NOT_IN_LIST);
        return 0;
    }
    /* un-link e from the chain. */
    if (e->next)
        e->next->prev = e->prev;
    if (e->prev)
        e->prev->next = e->next;
    /* Correct our head/tail if necessary. */
    if (engine_list_head == e)
        engine_list_head = e->next;
    if (engine_list_tail == e)
        engine_list_tail = e->prev;
    engine_free_util(e, 0);
    return 1;
}

/* Get the first/last "ENGINE" type available. */
ENGINE *ENGINE_get_first(void)
{
    ENGINE *ret;

    if (!RUN_ONCE(&engine_lock_init, do_engine_lock_init)) {
        ENGINEerr(ENGINE_F_ENGINE_GET_FIRST, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    CRYPTO_THREAD_write_lock(global_engine_lock);
    ret = engine_list_head;
    if (ret) {
        ret->struct_ref++;
        engine_ref_debug(ret, 0, 1);
    }
    CRYPTO_THREAD_unlock(global_engine_lock);
    return ret;
}

ENGINE *ENGINE_get_last(void)
{
    ENGINE *ret;

    if (!RUN_ONCE(&engine_lock_init, do_engine_lock_init)) {
        ENGINEerr(ENGINE_F_ENGINE_GET_LAST, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    CRYPTO_THREAD_write_lock(global_engine_lock);
    ret = engine_list_tail;
    if (ret) {
        ret->struct_ref++;
        engine_ref_debug(ret, 0, 1);
    }
    CRYPTO_THREAD_unlock(global_engine_lock);
    return ret;
}

/* Iterate to the next/previous "ENGINE" type (NULL = end of the list). */
ENGINE *ENGINE_get_next(ENGINE *e)
{
    ENGINE *ret = NULL;
    if (e == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_GET_NEXT, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    CRYPTO_THREAD_write_lock(global_engine_lock);
    ret = e->next;
    if (ret) {
        /* Return a valid structural reference to the next ENGINE */
        ret->struct_ref++;
        engine_ref_debug(ret, 0, 1);
    }
    CRYPTO_THREAD_unlock(global_engine_lock);
    /* Release the structural reference to the previous ENGINE */
    ENGINE_free(e);
    return ret;
}

ENGINE *ENGINE_get_prev(ENGINE *e)
{
    ENGINE *ret = NULL;
    if (e == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_GET_PREV, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    CRYPTO_THREAD_write_lock(global_engine_lock);
    ret = e->prev;
    if (ret) {
        /* Return a valid structural reference to the next ENGINE */
        ret->struct_ref++;
        engine_ref_debug(ret, 0, 1);
    }
    CRYPTO_THREAD_unlock(global_engine_lock);
    /* Release the structural reference to the previous ENGINE */
    ENGINE_free(e);
    return ret;
}

/* Add another "ENGINE" type into the list. */
int ENGINE_add(ENGINE *e)
{
    int to_return = 1;
    if (e == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_ADD, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    if ((e->id == NULL) || (e->name == NULL)) {
        ENGINEerr(ENGINE_F_ENGINE_ADD, ENGINE_R_ID_OR_NAME_MISSING);
        return 0;
    }
    CRYPTO_THREAD_write_lock(global_engine_lock);
    if (!engine_list_add(e)) {
        ENGINEerr(ENGINE_F_ENGINE_ADD, ENGINE_R_INTERNAL_LIST_ERROR);
        to_return = 0;
    }
    CRYPTO_THREAD_unlock(global_engine_lock);
    return to_return;
}

/* Remove an existing "ENGINE" type from the array. */
int ENGINE_remove(ENGINE *e)
{
    int to_return = 1;
    if (e == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_REMOVE, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    CRYPTO_THREAD_write_lock(global_engine_lock);
    if (!engine_list_remove(e)) {
        ENGINEerr(ENGINE_F_ENGINE_REMOVE, ENGINE_R_INTERNAL_LIST_ERROR);
        to_return = 0;
    }
    CRYPTO_THREAD_unlock(global_engine_lock);
    return to_return;
}

static void engine_cpy(ENGINE *dest, const ENGINE *src)
{
    dest->id = src->id;
    dest->name = src->name;
#ifndef OPENSSL_NO_RSA
    dest->rsa_meth = src->rsa_meth;
#endif
#ifndef OPENSSL_NO_DSA
    dest->dsa_meth = src->dsa_meth;
#endif
#ifndef OPENSSL_NO_DH
    dest->dh_meth = src->dh_meth;
#endif
#ifndef OPENSSL_NO_EC
    dest->ec_meth = src->ec_meth;
#endif
    dest->rand_meth = src->rand_meth;
    dest->ciphers = src->ciphers;
    dest->digests = src->digests;
    dest->pkey_meths = src->pkey_meths;
    dest->destroy = src->destroy;
    dest->init = src->init;
    dest->finish = src->finish;
    dest->ctrl = src->ctrl;
    dest->load_privkey = src->load_privkey;
    dest->load_pubkey = src->load_pubkey;
    dest->cmd_defns = src->cmd_defns;
    dest->flags = src->flags;
}

ENGINE *ENGINE_by_id(const char *id)
{
    ENGINE *iterator;
    char *load_dir = NULL;
    if (id == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_BY_ID, ERR_R_PASSED_NULL_PARAMETER);
        return NULL;
    }
    if (!RUN_ONCE(&engine_lock_init, do_engine_lock_init)) {
        ENGINEerr(ENGINE_F_ENGINE_BY_ID, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    CRYPTO_THREAD_write_lock(global_engine_lock);
    iterator = engine_list_head;
    while (iterator && (strcmp(id, iterator->id) != 0))
        iterator = iterator->next;
    if (iterator != NULL) {
        /*
         * We need to return a structural reference. If this is an ENGINE
         * type that returns copies, make a duplicate - otherwise increment
         * the existing ENGINE's reference count.
         */
        if (iterator->flags & ENGINE_FLAGS_BY_ID_COPY) {
            ENGINE *cp = ENGINE_new();
            if (cp == NULL)
                iterator = NULL;
            else {
                engine_cpy(cp, iterator);
                iterator = cp;
            }
        } else {
            iterator->struct_ref++;
            engine_ref_debug(iterator, 0, 1);
        }
    }
    CRYPTO_THREAD_unlock(global_engine_lock);
    if (iterator != NULL)
        return iterator;
    /*
     * Prevent infinite recursion if we're looking for the dynamic engine.
     */
    if (strcmp(id, "dynamic")) {
        if ((load_dir = ossl_safe_getenv("OPENSSL_ENGINES")) == NULL)
            load_dir = ENGINESDIR;
        iterator = ENGINE_by_id("dynamic");
        if (!iterator || !ENGINE_ctrl_cmd_string(iterator, "ID", id, 0) ||
            !ENGINE_ctrl_cmd_string(iterator, "DIR_LOAD", "2", 0) ||
            !ENGINE_ctrl_cmd_string(iterator, "DIR_ADD",
                                    load_dir, 0) ||
            !ENGINE_ctrl_cmd_string(iterator, "LIST_ADD", "1", 0) ||
            !ENGINE_ctrl_cmd_string(iterator, "LOAD", NULL, 0))
            goto notfound;
        return iterator;
    }
 notfound:
    ENGINE_free(iterator);
    ENGINEerr(ENGINE_F_ENGINE_BY_ID, ENGINE_R_NO_SUCH_ENGINE);
    ERR_add_error_data(2, "id=", id);
    return NULL;
    /* EEK! Experimental code ends */
}

int ENGINE_up_ref(ENGINE *e)
{
    int i;
    if (e == NULL) {
        ENGINEerr(ENGINE_F_ENGINE_UP_REF, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    CRYPTO_UP_REF(&e->struct_ref, &i, global_engine_lock);
    return 1;
}
