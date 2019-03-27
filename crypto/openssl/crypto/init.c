/*
 * Copyright 2016-2019 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "e_os.h"
#include "internal/cryptlib_int.h"
#include <openssl/err.h>
#include "internal/rand_int.h"
#include "internal/bio.h"
#include <openssl/evp.h>
#include "internal/evp_int.h"
#include "internal/conf.h"
#include "internal/async.h"
#include "internal/engine.h"
#include "internal/comp.h"
#include "internal/err.h"
#include "internal/err_int.h"
#include "internal/objects.h"
#include <stdlib.h>
#include <assert.h>
#include "internal/thread_once.h"
#include "internal/dso_conf.h"
#include "internal/dso.h"
#include "internal/store.h"

static int stopped = 0;

/*
 * Since per-thread-specific-data destructors are not universally
 * available, i.e. not on Windows, only below CRYPTO_THREAD_LOCAL key
 * is assumed to have destructor associated. And then an effort is made
 * to call this single destructor on non-pthread platform[s].
 *
 * Initial value is "impossible". It is used as guard value to shortcut
 * destructor for threads terminating before libcrypto is initialized or
 * after it's de-initialized. Access to the key doesn't have to be
 * serialized for the said threads, because they didn't use libcrypto
 * and it doesn't matter if they pick "impossible" or derefernce real
 * key value and pull NULL past initialization in the first thread that
 * intends to use libcrypto.
 */
static union {
    long sane;
    CRYPTO_THREAD_LOCAL value;
} destructor_key = { -1 };

static void ossl_init_thread_stop(struct thread_local_inits_st *locals);

static void ossl_init_thread_destructor(void *local)
{
    ossl_init_thread_stop((struct thread_local_inits_st *)local);
}

static struct thread_local_inits_st *ossl_init_get_thread_local(int alloc)
{
    struct thread_local_inits_st *local =
        CRYPTO_THREAD_get_local(&destructor_key.value);

    if (alloc) {
        if (local == NULL
            && (local = OPENSSL_zalloc(sizeof(*local))) != NULL
            && !CRYPTO_THREAD_set_local(&destructor_key.value, local)) {
            OPENSSL_free(local);
            return NULL;
        }
    } else {
        CRYPTO_THREAD_set_local(&destructor_key.value, NULL);
    }

    return local;
}

typedef struct ossl_init_stop_st OPENSSL_INIT_STOP;
struct ossl_init_stop_st {
    void (*handler)(void);
    OPENSSL_INIT_STOP *next;
};

static OPENSSL_INIT_STOP *stop_handlers = NULL;
static CRYPTO_RWLOCK *init_lock = NULL;

static CRYPTO_ONCE base = CRYPTO_ONCE_STATIC_INIT;
static int base_inited = 0;
DEFINE_RUN_ONCE_STATIC(ossl_init_base)
{
    CRYPTO_THREAD_LOCAL key;

#ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: ossl_init_base: Setting up stop handlers\n");
#endif
#ifndef OPENSSL_NO_CRYPTO_MDEBUG
    ossl_malloc_setup_failures();
#endif
    if (!CRYPTO_THREAD_init_local(&key, ossl_init_thread_destructor))
        return 0;
    if ((init_lock = CRYPTO_THREAD_lock_new()) == NULL)
        goto err;
    OPENSSL_cpuid_setup();

    destructor_key.value = key;
    base_inited = 1;
    return 1;

err:
#ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: ossl_init_base not ok!\n");
#endif
    CRYPTO_THREAD_lock_free(init_lock);
    init_lock = NULL;

    CRYPTO_THREAD_cleanup_local(&key);
    return 0;
}

static CRYPTO_ONCE register_atexit = CRYPTO_ONCE_STATIC_INIT;
#if !defined(OPENSSL_SYS_UEFI) && defined(_WIN32)
static int win32atexit(void)
{
    OPENSSL_cleanup();
    return 0;
}
#endif

DEFINE_RUN_ONCE_STATIC(ossl_init_register_atexit)
{
#ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: ossl_init_register_atexit()\n");
#endif
#ifndef OPENSSL_SYS_UEFI
# ifdef _WIN32
    /* We use _onexit() in preference because it gets called on DLL unload */
    if (_onexit(win32atexit) == NULL)
        return 0;
# else
    if (atexit(OPENSSL_cleanup) != 0)
        return 0;
# endif
#endif

    return 1;
}

DEFINE_RUN_ONCE_STATIC_ALT(ossl_init_no_register_atexit,
                           ossl_init_register_atexit)
{
#ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: ossl_init_no_register_atexit ok!\n");
#endif
    /* Do nothing in this case */
    return 1;
}

static CRYPTO_ONCE load_crypto_nodelete = CRYPTO_ONCE_STATIC_INIT;
DEFINE_RUN_ONCE_STATIC(ossl_init_load_crypto_nodelete)
{
#ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: ossl_init_load_crypto_nodelete()\n");
#endif
#if !defined(OPENSSL_NO_DSO) \
    && !defined(OPENSSL_USE_NODELETE) \
    && !defined(OPENSSL_NO_PINSHARED)
# ifdef DSO_WIN32
    {
        HMODULE handle = NULL;
        BOOL ret;

        /* We don't use the DSO route for WIN32 because there is a better way */
        ret = GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                (void *)&base_inited, &handle);

#  ifdef OPENSSL_INIT_DEBUG
        fprintf(stderr, "OPENSSL_INIT: obtained DSO reference? %s\n",
                (ret == TRUE ? "No!" : "Yes."));
#  endif
        return (ret == TRUE) ? 1 : 0;
    }
# else
    /*
     * Deliberately leak a reference to ourselves. This will force the library
     * to remain loaded until the atexit() handler is run at process exit.
     */
    {
        DSO *dso;
        void *err;

        if (!err_shelve_state(&err))
            return 0;

        dso = DSO_dsobyaddr(&base_inited, DSO_FLAG_NO_UNLOAD_ON_FREE);
#  ifdef OPENSSL_INIT_DEBUG
        fprintf(stderr, "OPENSSL_INIT: obtained DSO reference? %s\n",
                (dso == NULL ? "No!" : "Yes."));
        /*
         * In case of No!, it is uncertain our exit()-handlers can still be
         * called. After dlclose() the whole library might have been unloaded
         * already.
         */
#  endif
        DSO_free(dso);
        err_unshelve_state(err);
    }
# endif
#endif

    return 1;
}

static CRYPTO_ONCE load_crypto_strings = CRYPTO_ONCE_STATIC_INIT;
static int load_crypto_strings_inited = 0;
DEFINE_RUN_ONCE_STATIC(ossl_init_load_crypto_strings)
{
    int ret = 1;
    /*
     * OPENSSL_NO_AUTOERRINIT is provided here to prevent at compile time
     * pulling in all the error strings during static linking
     */
#if !defined(OPENSSL_NO_ERR) && !defined(OPENSSL_NO_AUTOERRINIT)
# ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: ossl_init_load_crypto_strings: "
                    "err_load_crypto_strings_int()\n");
# endif
    ret = err_load_crypto_strings_int();
    load_crypto_strings_inited = 1;
#endif
    return ret;
}

DEFINE_RUN_ONCE_STATIC_ALT(ossl_init_no_load_crypto_strings,
                           ossl_init_load_crypto_strings)
{
    /* Do nothing in this case */
    return 1;
}

static CRYPTO_ONCE add_all_ciphers = CRYPTO_ONCE_STATIC_INIT;
DEFINE_RUN_ONCE_STATIC(ossl_init_add_all_ciphers)
{
    /*
     * OPENSSL_NO_AUTOALGINIT is provided here to prevent at compile time
     * pulling in all the ciphers during static linking
     */
#ifndef OPENSSL_NO_AUTOALGINIT
# ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: ossl_init_add_all_ciphers: "
                    "openssl_add_all_ciphers_int()\n");
# endif
    openssl_add_all_ciphers_int();
#endif
    return 1;
}

DEFINE_RUN_ONCE_STATIC_ALT(ossl_init_no_add_all_ciphers,
                           ossl_init_add_all_ciphers)
{
    /* Do nothing */
    return 1;
}

static CRYPTO_ONCE add_all_digests = CRYPTO_ONCE_STATIC_INIT;
DEFINE_RUN_ONCE_STATIC(ossl_init_add_all_digests)
{
    /*
     * OPENSSL_NO_AUTOALGINIT is provided here to prevent at compile time
     * pulling in all the ciphers during static linking
     */
#ifndef OPENSSL_NO_AUTOALGINIT
# ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: ossl_init_add_all_digests: "
                    "openssl_add_all_digests()\n");
# endif
    openssl_add_all_digests_int();
#endif
    return 1;
}

DEFINE_RUN_ONCE_STATIC_ALT(ossl_init_no_add_all_digests,
                           ossl_init_add_all_digests)
{
    /* Do nothing */
    return 1;
}

static CRYPTO_ONCE config = CRYPTO_ONCE_STATIC_INIT;
static int config_inited = 0;
static const OPENSSL_INIT_SETTINGS *conf_settings = NULL;
DEFINE_RUN_ONCE_STATIC(ossl_init_config)
{
    int ret = openssl_config_int(conf_settings);
    config_inited = 1;
    return ret;
}
DEFINE_RUN_ONCE_STATIC_ALT(ossl_init_no_config, ossl_init_config)
{
#ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr,
            "OPENSSL_INIT: ossl_init_config: openssl_no_config_int()\n");
#endif
    openssl_no_config_int();
    config_inited = 1;
    return 1;
}

static CRYPTO_ONCE async = CRYPTO_ONCE_STATIC_INIT;
static int async_inited = 0;
DEFINE_RUN_ONCE_STATIC(ossl_init_async)
{
#ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: ossl_init_async: async_init()\n");
#endif
    if (!async_init())
        return 0;
    async_inited = 1;
    return 1;
}

#ifndef OPENSSL_NO_ENGINE
static CRYPTO_ONCE engine_openssl = CRYPTO_ONCE_STATIC_INIT;
DEFINE_RUN_ONCE_STATIC(ossl_init_engine_openssl)
{
# ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: ossl_init_engine_openssl: "
                    "engine_load_openssl_int()\n");
# endif
    engine_load_openssl_int();
    return 1;
}
# ifndef OPENSSL_NO_DEVCRYPTOENG
static CRYPTO_ONCE engine_devcrypto = CRYPTO_ONCE_STATIC_INIT;
DEFINE_RUN_ONCE_STATIC(ossl_init_engine_devcrypto)
{
#  ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: ossl_init_engine_devcrypto: "
                    "engine_load_devcrypto_int()\n");
#  endif
    engine_load_devcrypto_int();
    return 1;
}
# endif

# ifndef OPENSSL_NO_RDRAND
static CRYPTO_ONCE engine_rdrand = CRYPTO_ONCE_STATIC_INIT;
DEFINE_RUN_ONCE_STATIC(ossl_init_engine_rdrand)
{
#  ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: ossl_init_engine_rdrand: "
                    "engine_load_rdrand_int()\n");
#  endif
    engine_load_rdrand_int();
    return 1;
}
# endif
static CRYPTO_ONCE engine_dynamic = CRYPTO_ONCE_STATIC_INIT;
DEFINE_RUN_ONCE_STATIC(ossl_init_engine_dynamic)
{
# ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: ossl_init_engine_dynamic: "
                    "engine_load_dynamic_int()\n");
# endif
    engine_load_dynamic_int();
    return 1;
}
# ifndef OPENSSL_NO_STATIC_ENGINE
#  if !defined(OPENSSL_NO_HW) && !defined(OPENSSL_NO_HW_PADLOCK)
static CRYPTO_ONCE engine_padlock = CRYPTO_ONCE_STATIC_INIT;
DEFINE_RUN_ONCE_STATIC(ossl_init_engine_padlock)
{
#   ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: ossl_init_engine_padlock: "
                    "engine_load_padlock_int()\n");
#   endif
    engine_load_padlock_int();
    return 1;
}
#  endif
#  if defined(OPENSSL_SYS_WIN32) && !defined(OPENSSL_NO_CAPIENG)
static CRYPTO_ONCE engine_capi = CRYPTO_ONCE_STATIC_INIT;
DEFINE_RUN_ONCE_STATIC(ossl_init_engine_capi)
{
#   ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: ossl_init_engine_capi: "
                    "engine_load_capi_int()\n");
#   endif
    engine_load_capi_int();
    return 1;
}
#  endif
#  if !defined(OPENSSL_NO_AFALGENG)
static CRYPTO_ONCE engine_afalg = CRYPTO_ONCE_STATIC_INIT;
DEFINE_RUN_ONCE_STATIC(ossl_init_engine_afalg)
{
#   ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: ossl_init_engine_afalg: "
                    "engine_load_afalg_int()\n");
#   endif
    engine_load_afalg_int();
    return 1;
}
#  endif
# endif
#endif

#ifndef OPENSSL_NO_COMP
static CRYPTO_ONCE zlib = CRYPTO_ONCE_STATIC_INIT;

static int zlib_inited = 0;
DEFINE_RUN_ONCE_STATIC(ossl_init_zlib)
{
    /* Do nothing - we need to know about this for the later cleanup */
    zlib_inited = 1;
    return 1;
}
#endif

static void ossl_init_thread_stop(struct thread_local_inits_st *locals)
{
    /* Can't do much about this */
    if (locals == NULL)
        return;

    if (locals->async) {
#ifdef OPENSSL_INIT_DEBUG
        fprintf(stderr, "OPENSSL_INIT: ossl_init_thread_stop: "
                        "async_delete_thread_state()\n");
#endif
        async_delete_thread_state();
    }

    if (locals->err_state) {
#ifdef OPENSSL_INIT_DEBUG
        fprintf(stderr, "OPENSSL_INIT: ossl_init_thread_stop: "
                        "err_delete_thread_state()\n");
#endif
        err_delete_thread_state();
    }

    if (locals->rand) {
#ifdef OPENSSL_INIT_DEBUG
        fprintf(stderr, "OPENSSL_INIT: ossl_init_thread_stop: "
                        "drbg_delete_thread_state()\n");
#endif
        drbg_delete_thread_state();
    }

    OPENSSL_free(locals);
}

void OPENSSL_thread_stop(void)
{
    if (destructor_key.sane != -1)
        ossl_init_thread_stop(ossl_init_get_thread_local(0));
}

int ossl_init_thread_start(uint64_t opts)
{
    struct thread_local_inits_st *locals;

    if (!OPENSSL_init_crypto(0, NULL))
        return 0;

    locals = ossl_init_get_thread_local(1);

    if (locals == NULL)
        return 0;

    if (opts & OPENSSL_INIT_THREAD_ASYNC) {
#ifdef OPENSSL_INIT_DEBUG
        fprintf(stderr, "OPENSSL_INIT: ossl_init_thread_start: "
                        "marking thread for async\n");
#endif
        locals->async = 1;
    }

    if (opts & OPENSSL_INIT_THREAD_ERR_STATE) {
#ifdef OPENSSL_INIT_DEBUG
        fprintf(stderr, "OPENSSL_INIT: ossl_init_thread_start: "
                        "marking thread for err_state\n");
#endif
        locals->err_state = 1;
    }

    if (opts & OPENSSL_INIT_THREAD_RAND) {
#ifdef OPENSSL_INIT_DEBUG
        fprintf(stderr, "OPENSSL_INIT: ossl_init_thread_start: "
                        "marking thread for rand\n");
#endif
        locals->rand = 1;
    }

    return 1;
}

void OPENSSL_cleanup(void)
{
    OPENSSL_INIT_STOP *currhandler, *lasthandler;
    CRYPTO_THREAD_LOCAL key;

    /* If we've not been inited then no need to deinit */
    if (!base_inited)
        return;

    /* Might be explicitly called and also by atexit */
    if (stopped)
        return;
    stopped = 1;

    /*
     * Thread stop may not get automatically called by the thread library for
     * the very last thread in some situations, so call it directly.
     */
    ossl_init_thread_stop(ossl_init_get_thread_local(0));

    currhandler = stop_handlers;
    while (currhandler != NULL) {
        currhandler->handler();
        lasthandler = currhandler;
        currhandler = currhandler->next;
        OPENSSL_free(lasthandler);
    }
    stop_handlers = NULL;

    CRYPTO_THREAD_lock_free(init_lock);
    init_lock = NULL;

    /*
     * We assume we are single-threaded for this function, i.e. no race
     * conditions for the various "*_inited" vars below.
     */

#ifndef OPENSSL_NO_COMP
    if (zlib_inited) {
#ifdef OPENSSL_INIT_DEBUG
        fprintf(stderr, "OPENSSL_INIT: OPENSSL_cleanup: "
                        "comp_zlib_cleanup_int()\n");
#endif
        comp_zlib_cleanup_int();
    }
#endif

    if (async_inited) {
# ifdef OPENSSL_INIT_DEBUG
        fprintf(stderr, "OPENSSL_INIT: OPENSSL_cleanup: "
                        "async_deinit()\n");
# endif
        async_deinit();
    }

    if (load_crypto_strings_inited) {
#ifdef OPENSSL_INIT_DEBUG
        fprintf(stderr, "OPENSSL_INIT: OPENSSL_cleanup: "
                        "err_free_strings_int()\n");
#endif
        err_free_strings_int();
    }

    key = destructor_key.value;
    destructor_key.sane = -1;
    CRYPTO_THREAD_cleanup_local(&key);

#ifdef OPENSSL_INIT_DEBUG
    fprintf(stderr, "OPENSSL_INIT: OPENSSL_cleanup: "
                    "rand_cleanup_int()\n");
    fprintf(stderr, "OPENSSL_INIT: OPENSSL_cleanup: "
                    "conf_modules_free_int()\n");
#ifndef OPENSSL_NO_ENGINE
    fprintf(stderr, "OPENSSL_INIT: OPENSSL_cleanup: "
                    "engine_cleanup_int()\n");
#endif
    fprintf(stderr, "OPENSSL_INIT: OPENSSL_cleanup: "
                    "crypto_cleanup_all_ex_data_int()\n");
    fprintf(stderr, "OPENSSL_INIT: OPENSSL_cleanup: "
                    "bio_sock_cleanup_int()\n");
    fprintf(stderr, "OPENSSL_INIT: OPENSSL_cleanup: "
                    "bio_cleanup()\n");
    fprintf(stderr, "OPENSSL_INIT: OPENSSL_cleanup: "
                    "evp_cleanup_int()\n");
    fprintf(stderr, "OPENSSL_INIT: OPENSSL_cleanup: "
                    "obj_cleanup_int()\n");
    fprintf(stderr, "OPENSSL_INIT: OPENSSL_cleanup: "
                    "err_cleanup()\n");
#endif
    /*
     * Note that cleanup order is important:
     * - rand_cleanup_int could call an ENGINE's RAND cleanup function so
     * must be called before engine_cleanup_int()
     * - ENGINEs use CRYPTO_EX_DATA and therefore, must be cleaned up
     * before the ex data handlers are wiped in CRYPTO_cleanup_all_ex_data().
     * - conf_modules_free_int() can end up in ENGINE code so must be called
     * before engine_cleanup_int()
     * - ENGINEs and additional EVP algorithms might use added OIDs names so
     * obj_cleanup_int() must be called last
     */
    rand_cleanup_int();
    rand_drbg_cleanup_int();
    conf_modules_free_int();
#ifndef OPENSSL_NO_ENGINE
    engine_cleanup_int();
#endif
    ossl_store_cleanup_int();
    crypto_cleanup_all_ex_data_int();
    bio_cleanup();
    evp_cleanup_int();
    obj_cleanup_int();
    err_cleanup();

    CRYPTO_secure_malloc_done();

    base_inited = 0;
}

/*
 * If this function is called with a non NULL settings value then it must be
 * called prior to any threads making calls to any OpenSSL functions,
 * i.e. passing a non-null settings value is assumed to be single-threaded.
 */
int OPENSSL_init_crypto(uint64_t opts, const OPENSSL_INIT_SETTINGS *settings)
{
    if (stopped) {
        if (!(opts & OPENSSL_INIT_BASE_ONLY))
            CRYPTOerr(CRYPTO_F_OPENSSL_INIT_CRYPTO, ERR_R_INIT_FAIL);
        return 0;
    }

    /*
     * When the caller specifies OPENSSL_INIT_BASE_ONLY, that should be the
     * *only* option specified.  With that option we return immediately after
     * doing the requested limited initialization.  Note that
     * err_shelve_state() called by us via ossl_init_load_crypto_nodelete()
     * re-enters OPENSSL_init_crypto() with OPENSSL_INIT_BASE_ONLY, but with
     * base already initialized this is a harmless NOOP.
     *
     * If we remain the only caller of err_shelve_state() the recursion should
     * perhaps be removed, but if in doubt, it can be left in place.
     */
    if (!RUN_ONCE(&base, ossl_init_base))
        return 0;
    if (opts & OPENSSL_INIT_BASE_ONLY)
        return 1;

    /*
     * Now we don't always set up exit handlers, the INIT_BASE_ONLY calls
     * should not have the side-effect of setting up exit handlers, and
     * therefore, this code block is below the INIT_BASE_ONLY-conditioned early
     * return above.
     */
    if ((opts & OPENSSL_INIT_NO_ATEXIT) != 0) {
        if (!RUN_ONCE_ALT(&register_atexit, ossl_init_no_register_atexit,
                          ossl_init_register_atexit))
            return 0;
    } else if (!RUN_ONCE(&register_atexit, ossl_init_register_atexit)) {
        return 0;
    }

    if (!RUN_ONCE(&load_crypto_nodelete, ossl_init_load_crypto_nodelete))
        return 0;

    if ((opts & OPENSSL_INIT_NO_LOAD_CRYPTO_STRINGS)
            && !RUN_ONCE_ALT(&load_crypto_strings,
                             ossl_init_no_load_crypto_strings,
                             ossl_init_load_crypto_strings))
        return 0;

    if ((opts & OPENSSL_INIT_LOAD_CRYPTO_STRINGS)
            && !RUN_ONCE(&load_crypto_strings, ossl_init_load_crypto_strings))
        return 0;

    if ((opts & OPENSSL_INIT_NO_ADD_ALL_CIPHERS)
            && !RUN_ONCE_ALT(&add_all_ciphers, ossl_init_no_add_all_ciphers,
                             ossl_init_add_all_ciphers))
        return 0;

    if ((opts & OPENSSL_INIT_ADD_ALL_CIPHERS)
            && !RUN_ONCE(&add_all_ciphers, ossl_init_add_all_ciphers))
        return 0;

    if ((opts & OPENSSL_INIT_NO_ADD_ALL_DIGESTS)
            && !RUN_ONCE_ALT(&add_all_digests, ossl_init_no_add_all_digests,
                             ossl_init_add_all_digests))
        return 0;

    if ((opts & OPENSSL_INIT_ADD_ALL_DIGESTS)
            && !RUN_ONCE(&add_all_digests, ossl_init_add_all_digests))
        return 0;

    if ((opts & OPENSSL_INIT_ATFORK)
            && !openssl_init_fork_handlers())
        return 0;

    if ((opts & OPENSSL_INIT_NO_LOAD_CONFIG)
            && !RUN_ONCE_ALT(&config, ossl_init_no_config, ossl_init_config))
        return 0;

    if (opts & OPENSSL_INIT_LOAD_CONFIG) {
        int ret;
        CRYPTO_THREAD_write_lock(init_lock);
        conf_settings = settings;
        ret = RUN_ONCE(&config, ossl_init_config);
        conf_settings = NULL;
        CRYPTO_THREAD_unlock(init_lock);
        if (!ret)
            return 0;
    }

    if ((opts & OPENSSL_INIT_ASYNC)
            && !RUN_ONCE(&async, ossl_init_async))
        return 0;

#ifndef OPENSSL_NO_ENGINE
    if ((opts & OPENSSL_INIT_ENGINE_OPENSSL)
            && !RUN_ONCE(&engine_openssl, ossl_init_engine_openssl))
        return 0;
# if !defined(OPENSSL_NO_HW) && !defined(OPENSSL_NO_DEVCRYPTOENG)
    if ((opts & OPENSSL_INIT_ENGINE_CRYPTODEV)
            && !RUN_ONCE(&engine_devcrypto, ossl_init_engine_devcrypto))
        return 0;
# endif
# ifndef OPENSSL_NO_RDRAND
    if ((opts & OPENSSL_INIT_ENGINE_RDRAND)
            && !RUN_ONCE(&engine_rdrand, ossl_init_engine_rdrand))
        return 0;
# endif
    if ((opts & OPENSSL_INIT_ENGINE_DYNAMIC)
            && !RUN_ONCE(&engine_dynamic, ossl_init_engine_dynamic))
        return 0;
# ifndef OPENSSL_NO_STATIC_ENGINE
#  if !defined(OPENSSL_NO_HW) && !defined(OPENSSL_NO_HW_PADLOCK)
    if ((opts & OPENSSL_INIT_ENGINE_PADLOCK)
            && !RUN_ONCE(&engine_padlock, ossl_init_engine_padlock))
        return 0;
#  endif
#  if defined(OPENSSL_SYS_WIN32) && !defined(OPENSSL_NO_CAPIENG)
    if ((opts & OPENSSL_INIT_ENGINE_CAPI)
            && !RUN_ONCE(&engine_capi, ossl_init_engine_capi))
        return 0;
#  endif
#  if !defined(OPENSSL_NO_AFALGENG)
    if ((opts & OPENSSL_INIT_ENGINE_AFALG)
            && !RUN_ONCE(&engine_afalg, ossl_init_engine_afalg))
        return 0;
#  endif
# endif
    if (opts & (OPENSSL_INIT_ENGINE_ALL_BUILTIN
                | OPENSSL_INIT_ENGINE_OPENSSL
                | OPENSSL_INIT_ENGINE_AFALG)) {
        ENGINE_register_all_complete();
    }
#endif

#ifndef OPENSSL_NO_COMP
    if ((opts & OPENSSL_INIT_ZLIB)
            && !RUN_ONCE(&zlib, ossl_init_zlib))
        return 0;
#endif

    return 1;
}

int OPENSSL_atexit(void (*handler)(void))
{
    OPENSSL_INIT_STOP *newhand;

#if !defined(OPENSSL_NO_DSO) \
    && !defined(OPENSSL_USE_NODELETE)\
    && !defined(OPENSSL_NO_PINSHARED)
    {
        union {
            void *sym;
            void (*func)(void);
        } handlersym;

        handlersym.func = handler;
# ifdef DSO_WIN32
        {
            HMODULE handle = NULL;
            BOOL ret;

            /*
             * We don't use the DSO route for WIN32 because there is a better
             * way
             */
            ret = GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                    | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                    handlersym.sym, &handle);

            if (!ret)
                return 0;
        }
# else
        /*
         * Deliberately leak a reference to the handler. This will force the
         * library/code containing the handler to remain loaded until we run the
         * atexit handler. If -znodelete has been used then this is
         * unnecessary.
         */
        {
            DSO *dso = NULL;

            ERR_set_mark();
            dso = DSO_dsobyaddr(handlersym.sym, DSO_FLAG_NO_UNLOAD_ON_FREE);
#  ifdef OPENSSL_INIT_DEBUG
            fprintf(stderr,
                    "OPENSSL_INIT: OPENSSL_atexit: obtained DSO reference? %s\n",
                    (dso == NULL ? "No!" : "Yes."));
            /* See same code above in ossl_init_base() for an explanation. */
#  endif
            DSO_free(dso);
            ERR_pop_to_mark();
        }
# endif
    }
#endif

    if ((newhand = OPENSSL_malloc(sizeof(*newhand))) == NULL) {
        CRYPTOerr(CRYPTO_F_OPENSSL_ATEXIT, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    newhand->handler = handler;
    newhand->next = stop_handlers;
    stop_handlers = newhand;

    return 1;
}

#ifdef OPENSSL_SYS_UNIX
/*
 * The following three functions are for OpenSSL developers.  This is
 * where we set/reset state across fork (called via pthread_atfork when
 * it exists, or manually by the application when it doesn't).
 *
 * WARNING!  If you put code in either OPENSSL_fork_parent or
 * OPENSSL_fork_child, you MUST MAKE SURE that they are async-signal-
 * safe.  See this link, for example:
 *      http://man7.org/linux/man-pages/man7/signal-safety.7.html
 */

void OPENSSL_fork_prepare(void)
{
}

void OPENSSL_fork_parent(void)
{
}

void OPENSSL_fork_child(void)
{
    rand_fork();
}
#endif
