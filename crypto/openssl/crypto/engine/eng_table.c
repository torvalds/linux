/*
 * Copyright 2001-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/cryptlib.h"
#include <openssl/evp.h>
#include <openssl/lhash.h>
#include "eng_int.h"

/* The type of the items in the table */
struct st_engine_pile {
    /* The 'nid' of this algorithm/mode */
    int nid;
    /* ENGINEs that implement this algorithm/mode. */
    STACK_OF(ENGINE) *sk;
    /* The default ENGINE to perform this algorithm/mode. */
    ENGINE *funct;
    /*
     * Zero if 'sk' is newer than the cached 'funct', non-zero otherwise
     */
    int uptodate;
};

/* The type exposed in eng_int.h */
struct st_engine_table {
    LHASH_OF(ENGINE_PILE) piles;
};                              /* ENGINE_TABLE */

typedef struct st_engine_pile_doall {
    engine_table_doall_cb *cb;
    void *arg;
} ENGINE_PILE_DOALL;

/* Global flags (ENGINE_TABLE_FLAG_***). */
static unsigned int table_flags = 0;

/* API function manipulating 'table_flags' */
unsigned int ENGINE_get_table_flags(void)
{
    return table_flags;
}

void ENGINE_set_table_flags(unsigned int flags)
{
    table_flags = flags;
}

/* Internal functions for the "piles" hash table */
static unsigned long engine_pile_hash(const ENGINE_PILE *c)
{
    return c->nid;
}

static int engine_pile_cmp(const ENGINE_PILE *a, const ENGINE_PILE *b)
{
    return a->nid - b->nid;
}

static int int_table_check(ENGINE_TABLE **t, int create)
{
    LHASH_OF(ENGINE_PILE) *lh;

    if (*t)
        return 1;
    if (!create)
        return 0;
    if ((lh = lh_ENGINE_PILE_new(engine_pile_hash, engine_pile_cmp)) == NULL)
        return 0;
    *t = (ENGINE_TABLE *)lh;
    return 1;
}

/*
 * Privately exposed (via eng_int.h) functions for adding and/or removing
 * ENGINEs from the implementation table
 */
int engine_table_register(ENGINE_TABLE **table, ENGINE_CLEANUP_CB *cleanup,
                          ENGINE *e, const int *nids, int num_nids,
                          int setdefault)
{
    int ret = 0, added = 0;
    ENGINE_PILE tmplate, *fnd;
    CRYPTO_THREAD_write_lock(global_engine_lock);
    if (!(*table))
        added = 1;
    if (!int_table_check(table, 1))
        goto end;
    if (added)
        /* The cleanup callback needs to be added */
        engine_cleanup_add_first(cleanup);
    while (num_nids--) {
        tmplate.nid = *nids;
        fnd = lh_ENGINE_PILE_retrieve(&(*table)->piles, &tmplate);
        if (!fnd) {
            fnd = OPENSSL_malloc(sizeof(*fnd));
            if (fnd == NULL)
                goto end;
            fnd->uptodate = 1;
            fnd->nid = *nids;
            fnd->sk = sk_ENGINE_new_null();
            if (!fnd->sk) {
                OPENSSL_free(fnd);
                goto end;
            }
            fnd->funct = NULL;
            (void)lh_ENGINE_PILE_insert(&(*table)->piles, fnd);
            if (lh_ENGINE_PILE_retrieve(&(*table)->piles, &tmplate) != fnd) {
                sk_ENGINE_free(fnd->sk);
                OPENSSL_free(fnd);
                goto end;
            }
        }
        /* A registration shouldn't add duplicate entries */
        (void)sk_ENGINE_delete_ptr(fnd->sk, e);
        /*
         * if 'setdefault', this ENGINE goes to the head of the list
         */
        if (!sk_ENGINE_push(fnd->sk, e))
            goto end;
        /* "touch" this ENGINE_PILE */
        fnd->uptodate = 0;
        if (setdefault) {
            if (!engine_unlocked_init(e)) {
                ENGINEerr(ENGINE_F_ENGINE_TABLE_REGISTER,
                          ENGINE_R_INIT_FAILED);
                goto end;
            }
            if (fnd->funct)
                engine_unlocked_finish(fnd->funct, 0);
            fnd->funct = e;
            fnd->uptodate = 1;
        }
        nids++;
    }
    ret = 1;
 end:
    CRYPTO_THREAD_unlock(global_engine_lock);
    return ret;
}

static void int_unregister_cb(ENGINE_PILE *pile, ENGINE *e)
{
    int n;
    /* Iterate the 'c->sk' stack removing any occurrence of 'e' */
    while ((n = sk_ENGINE_find(pile->sk, e)) >= 0) {
        (void)sk_ENGINE_delete(pile->sk, n);
        pile->uptodate = 0;
    }
    if (pile->funct == e) {
        engine_unlocked_finish(e, 0);
        pile->funct = NULL;
    }
}

IMPLEMENT_LHASH_DOALL_ARG(ENGINE_PILE, ENGINE);

void engine_table_unregister(ENGINE_TABLE **table, ENGINE *e)
{
    CRYPTO_THREAD_write_lock(global_engine_lock);
    if (int_table_check(table, 0))
        lh_ENGINE_PILE_doall_ENGINE(&(*table)->piles, int_unregister_cb, e);
    CRYPTO_THREAD_unlock(global_engine_lock);
}

static void int_cleanup_cb_doall(ENGINE_PILE *p)
{
    if (!p)
        return;
    sk_ENGINE_free(p->sk);
    if (p->funct)
        engine_unlocked_finish(p->funct, 0);
    OPENSSL_free(p);
}

void engine_table_cleanup(ENGINE_TABLE **table)
{
    CRYPTO_THREAD_write_lock(global_engine_lock);
    if (*table) {
        lh_ENGINE_PILE_doall(&(*table)->piles, int_cleanup_cb_doall);
        lh_ENGINE_PILE_free(&(*table)->piles);
        *table = NULL;
    }
    CRYPTO_THREAD_unlock(global_engine_lock);
}

/* return a functional reference for a given 'nid' */
#ifndef ENGINE_TABLE_DEBUG
ENGINE *engine_table_select(ENGINE_TABLE **table, int nid)
#else
ENGINE *engine_table_select_tmp(ENGINE_TABLE **table, int nid, const char *f,
                                int l)
#endif
{
    ENGINE *ret = NULL;
    ENGINE_PILE tmplate, *fnd = NULL;
    int initres, loop = 0;

    if (!(*table)) {
#ifdef ENGINE_TABLE_DEBUG
        fprintf(stderr, "engine_table_dbg: %s:%d, nid=%d, nothing "
                "registered!\n", f, l, nid);
#endif
        return NULL;
    }
    ERR_set_mark();
    CRYPTO_THREAD_write_lock(global_engine_lock);
    /*
     * Check again inside the lock otherwise we could race against cleanup
     * operations. But don't worry about a fprintf(stderr).
     */
    if (!int_table_check(table, 0))
        goto end;
    tmplate.nid = nid;
    fnd = lh_ENGINE_PILE_retrieve(&(*table)->piles, &tmplate);
    if (!fnd)
        goto end;
    if (fnd->funct && engine_unlocked_init(fnd->funct)) {
#ifdef ENGINE_TABLE_DEBUG
        fprintf(stderr, "engine_table_dbg: %s:%d, nid=%d, using "
                "ENGINE '%s' cached\n", f, l, nid, fnd->funct->id);
#endif
        ret = fnd->funct;
        goto end;
    }
    if (fnd->uptodate) {
        ret = fnd->funct;
        goto end;
    }
 trynext:
    ret = sk_ENGINE_value(fnd->sk, loop++);
    if (!ret) {
#ifdef ENGINE_TABLE_DEBUG
        fprintf(stderr, "engine_table_dbg: %s:%d, nid=%d, no "
                "registered implementations would initialise\n", f, l, nid);
#endif
        goto end;
    }
    /* Try to initialise the ENGINE? */
    if ((ret->funct_ref > 0) || !(table_flags & ENGINE_TABLE_FLAG_NOINIT))
        initres = engine_unlocked_init(ret);
    else
        initres = 0;
    if (initres) {
        /* Update 'funct' */
        if ((fnd->funct != ret) && engine_unlocked_init(ret)) {
            /* If there was a previous default we release it. */
            if (fnd->funct)
                engine_unlocked_finish(fnd->funct, 0);
            fnd->funct = ret;
#ifdef ENGINE_TABLE_DEBUG
            fprintf(stderr, "engine_table_dbg: %s:%d, nid=%d, "
                    "setting default to '%s'\n", f, l, nid, ret->id);
#endif
        }
#ifdef ENGINE_TABLE_DEBUG
        fprintf(stderr, "engine_table_dbg: %s:%d, nid=%d, using "
                "newly initialised '%s'\n", f, l, nid, ret->id);
#endif
        goto end;
    }
    goto trynext;
 end:
    /*
     * If it failed, it is unlikely to succeed again until some future
     * registrations have taken place. In all cases, we cache.
     */
    if (fnd)
        fnd->uptodate = 1;
#ifdef ENGINE_TABLE_DEBUG
    if (ret)
        fprintf(stderr, "engine_table_dbg: %s:%d, nid=%d, caching "
                "ENGINE '%s'\n", f, l, nid, ret->id);
    else
        fprintf(stderr, "engine_table_dbg: %s:%d, nid=%d, caching "
                "'no matching ENGINE'\n", f, l, nid);
#endif
    CRYPTO_THREAD_unlock(global_engine_lock);
    /*
     * Whatever happened, any failed init()s are not failures in this
     * context, so clear our error state.
     */
    ERR_pop_to_mark();
    return ret;
}

/* Table enumeration */

static void int_dall(const ENGINE_PILE *pile, ENGINE_PILE_DOALL *dall)
{
    dall->cb(pile->nid, pile->sk, pile->funct, dall->arg);
}

IMPLEMENT_LHASH_DOALL_ARG_CONST(ENGINE_PILE, ENGINE_PILE_DOALL);

void engine_table_doall(ENGINE_TABLE *table, engine_table_doall_cb *cb,
                        void *arg)
{
    ENGINE_PILE_DOALL dall;
    dall.cb = cb;
    dall.arg = arg;
    if (table)
        lh_ENGINE_PILE_doall_ENGINE_PILE_DOALL(&table->piles, int_dall, &dall);
}
