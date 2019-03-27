/*
 * Copyright 2001-2018 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright (c) 2002, Oracle and/or its affiliates. All rights reserved
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_ENGINE_INT_H
# define HEADER_ENGINE_INT_H

# include "internal/cryptlib.h"
# include "internal/engine.h"
# include "internal/thread_once.h"
# include "internal/refcount.h"

extern CRYPTO_RWLOCK *global_engine_lock;

/*
 * If we compile with this symbol defined, then both reference counts in the
 * ENGINE structure will be monitored with a line of output on stderr for
 * each change. This prints the engine's pointer address (truncated to
 * unsigned int), "struct" or "funct" to indicate the reference type, the
 * before and after reference count, and the file:line-number pair. The
 * "engine_ref_debug" statements must come *after* the change.
 */
# ifdef ENGINE_REF_COUNT_DEBUG

#  define engine_ref_debug(e, isfunct, diff) \
        fprintf(stderr, "engine: %08x %s from %d to %d (%s:%d)\n", \
                (unsigned int)(e), (isfunct ? "funct" : "struct"), \
                ((isfunct) ? ((e)->funct_ref - (diff)) : ((e)->struct_ref - (diff))), \
                ((isfunct) ? (e)->funct_ref : (e)->struct_ref), \
                (OPENSSL_FILE), (OPENSSL_LINE))

# else

#  define engine_ref_debug(e, isfunct, diff)

# endif

/*
 * Any code that will need cleanup operations should use these functions to
 * register callbacks. engine_cleanup_int() will call all registered
 * callbacks in order. NB: both the "add" functions assume the engine lock to
 * already be held (in "write" mode).
 */
typedef void (ENGINE_CLEANUP_CB) (void);
typedef struct st_engine_cleanup_item {
    ENGINE_CLEANUP_CB *cb;
} ENGINE_CLEANUP_ITEM;
DEFINE_STACK_OF(ENGINE_CLEANUP_ITEM)
void engine_cleanup_add_first(ENGINE_CLEANUP_CB *cb);
void engine_cleanup_add_last(ENGINE_CLEANUP_CB *cb);

/* We need stacks of ENGINEs for use in eng_table.c */
DEFINE_STACK_OF(ENGINE)

/*
 * If this symbol is defined then engine_table_select(), the function that is
 * used by RSA, DSA (etc) code to select registered ENGINEs, cache defaults
 * and functional references (etc), will display debugging summaries to
 * stderr.
 */
/* #define ENGINE_TABLE_DEBUG */

/*
 * This represents an implementation table. Dependent code should instantiate
 * it as a (ENGINE_TABLE *) pointer value set initially to NULL.
 */
typedef struct st_engine_table ENGINE_TABLE;
int engine_table_register(ENGINE_TABLE **table, ENGINE_CLEANUP_CB *cleanup,
                          ENGINE *e, const int *nids, int num_nids,
                          int setdefault);
void engine_table_unregister(ENGINE_TABLE **table, ENGINE *e);
void engine_table_cleanup(ENGINE_TABLE **table);
# ifndef ENGINE_TABLE_DEBUG
ENGINE *engine_table_select(ENGINE_TABLE **table, int nid);
# else
ENGINE *engine_table_select_tmp(ENGINE_TABLE **table, int nid, const char *f,
                                int l);
#  define engine_table_select(t,n) engine_table_select_tmp(t,n,OPENSSL_FILE,OPENSSL_LINE)
# endif
typedef void (engine_table_doall_cb) (int nid, STACK_OF(ENGINE) *sk,
                                      ENGINE *def, void *arg);
void engine_table_doall(ENGINE_TABLE *table, engine_table_doall_cb *cb,
                        void *arg);

/*
 * Internal versions of API functions that have control over locking. These
 * are used between C files when functionality needs to be shared but the
 * caller may already be controlling of the engine lock.
 */
int engine_unlocked_init(ENGINE *e);
int engine_unlocked_finish(ENGINE *e, int unlock_for_handlers);
int engine_free_util(ENGINE *e, int not_locked);

/*
 * This function will reset all "set"able values in an ENGINE to NULL. This
 * won't touch reference counts or ex_data, but is equivalent to calling all
 * the ENGINE_set_***() functions with a NULL value.
 */
void engine_set_all_null(ENGINE *e);

/*
 * NB: Bitwise OR-able values for the "flags" variable in ENGINE are now
 * exposed in engine.h.
 */

/* Free up dynamically allocated public key methods associated with ENGINE */

void engine_pkey_meths_free(ENGINE *e);
void engine_pkey_asn1_meths_free(ENGINE *e);

/* Once initialisation function */
extern CRYPTO_ONCE engine_lock_init;
DECLARE_RUN_ONCE(do_engine_lock_init)

/*
 * This is a structure for storing implementations of various crypto
 * algorithms and functions.
 */
struct engine_st {
    const char *id;
    const char *name;
    const RSA_METHOD *rsa_meth;
    const DSA_METHOD *dsa_meth;
    const DH_METHOD *dh_meth;
    const EC_KEY_METHOD *ec_meth;
    const RAND_METHOD *rand_meth;
    /* Cipher handling is via this callback */
    ENGINE_CIPHERS_PTR ciphers;
    /* Digest handling is via this callback */
    ENGINE_DIGESTS_PTR digests;
    /* Public key handling via this callback */
    ENGINE_PKEY_METHS_PTR pkey_meths;
    /* ASN1 public key handling via this callback */
    ENGINE_PKEY_ASN1_METHS_PTR pkey_asn1_meths;
    ENGINE_GEN_INT_FUNC_PTR destroy;
    ENGINE_GEN_INT_FUNC_PTR init;
    ENGINE_GEN_INT_FUNC_PTR finish;
    ENGINE_CTRL_FUNC_PTR ctrl;
    ENGINE_LOAD_KEY_PTR load_privkey;
    ENGINE_LOAD_KEY_PTR load_pubkey;
    ENGINE_SSL_CLIENT_CERT_PTR load_ssl_client_cert;
    const ENGINE_CMD_DEFN *cmd_defns;
    int flags;
    /* reference count on the structure itself */
    CRYPTO_REF_COUNT struct_ref;
    /*
     * reference count on usability of the engine type. NB: This controls the
     * loading and initialisation of any functionality required by this
     * engine, whereas the previous count is simply to cope with
     * (de)allocation of this structure. Hence, running_ref <= struct_ref at
     * all times.
     */
    int funct_ref;
    /* A place to store per-ENGINE data */
    CRYPTO_EX_DATA ex_data;
    /* Used to maintain the linked-list of engines. */
    struct engine_st *prev;
    struct engine_st *next;
};

typedef struct st_engine_pile ENGINE_PILE;

DEFINE_LHASH_OF(ENGINE_PILE);

#endif                          /* HEADER_ENGINE_INT_H */
