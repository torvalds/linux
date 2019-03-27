/*
 * Copyright 1995-2019 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/crypto.h>

/*
 * DEFINE_RUN_ONCE: Define an initialiser function that should be run exactly
 * once. It takes no arguments and returns and int result (1 for success or
 * 0 for failure). Typical usage might be:
 *
 * DEFINE_RUN_ONCE(myinitfunc)
 * {
 *     do_some_initialisation();
 *     if (init_is_successful())
 *         return 1;
 *
 *     return 0;
 * }
 */
#define DEFINE_RUN_ONCE(init)                   \
    static int init(void);                     \
    int init##_ossl_ret_ = 0;                   \
    void init##_ossl_(void)                     \
    {                                           \
        init##_ossl_ret_ = init();              \
    }                                           \
    static int init(void)

/*
 * DECLARE_RUN_ONCE: Declare an initialiser function that should be run exactly
 * once that has been defined in another file via DEFINE_RUN_ONCE().
 */
#define DECLARE_RUN_ONCE(init)                  \
    extern int init##_ossl_ret_;                \
    void init##_ossl_(void);

/*
 * DEFINE_RUN_ONCE_STATIC: Define an initialiser function that should be run
 * exactly once. This function will be declared as static within the file. It
 * takes no arguments and returns and int result (1 for success or 0 for
 * failure). Typical usage might be:
 *
 * DEFINE_RUN_ONCE_STATIC(myinitfunc)
 * {
 *     do_some_initialisation();
 *     if (init_is_successful())
 *         return 1;
 *
 *     return 0;
 * }
 */
#define DEFINE_RUN_ONCE_STATIC(init)            \
    static int init(void);                     \
    static int init##_ossl_ret_ = 0;            \
    static void init##_ossl_(void)              \
    {                                           \
        init##_ossl_ret_ = init();              \
    }                                           \
    static int init(void)

/*
 * DEFINE_RUN_ONCE_STATIC_ALT: Define an alternative initialiser function. This
 * function will be declared as static within the file. It takes no arguments
 * and returns an int result (1 for success or 0 for failure). An alternative
 * initialiser function is expected to be associated with a primary initialiser
 * function defined via DEFINE_ONCE_STATIC where both functions use the same
 * CRYPTO_ONCE object to synchronise. Where an alternative initialiser function
 * is used only one of the primary or the alternative initialiser function will
 * ever be called - and that function will be called exactly once. Definitition
 * of an alternative initialiser function MUST occur AFTER the definition of the
 * primary initialiser function.
 *
 * Typical usage might be:
 *
 * DEFINE_RUN_ONCE_STATIC(myinitfunc)
 * {
 *     do_some_initialisation();
 *     if (init_is_successful())
 *         return 1;
 *
 *     return 0;
 * }
 *
 * DEFINE_RUN_ONCE_STATIC_ALT(myaltinitfunc, myinitfunc)
 * {
 *     do_some_alternative_initialisation();
 *     if (init_is_successful())
 *         return 1;
 *
 *     return 0;
 * }
 */
#define DEFINE_RUN_ONCE_STATIC_ALT(initalt, init) \
    static int initalt(void);                     \
    static void initalt##_ossl_(void)             \
    {                                             \
        init##_ossl_ret_ = initalt();             \
    }                                             \
    static int initalt(void)

/*
 * RUN_ONCE - use CRYPTO_THREAD_run_once, and check if the init succeeded
 * @once: pointer to static object of type CRYPTO_ONCE
 * @init: function name that was previously given to DEFINE_RUN_ONCE,
 *        DEFINE_RUN_ONCE_STATIC or DECLARE_RUN_ONCE.  This function
 *        must return 1 for success or 0 for failure.
 *
 * The return value is 1 on success (*) or 0 in case of error.
 *
 * (*) by convention, since the init function must return 1 on success.
 */
#define RUN_ONCE(once, init)                                            \
    (CRYPTO_THREAD_run_once(once, init##_ossl_) ? init##_ossl_ret_ : 0)

/*
 * RUN_ONCE_ALT - use CRYPTO_THREAD_run_once, to run an alternative initialiser
 *                function and check if that initialisation succeeded
 * @once:    pointer to static object of type CRYPTO_ONCE
 * @initalt: alternative initialiser function name that was previously given to
 *           DEFINE_RUN_ONCE_STATIC_ALT.  This function must return 1 for
 *           success or 0 for failure.
 * @init:    primary initialiser function name that was previously given to
 *           DEFINE_RUN_ONCE_STATIC.  This function must return 1 for success or
 *           0 for failure.
 *
 * The return value is 1 on success (*) or 0 in case of error.
 *
 * (*) by convention, since the init function must return 1 on success.
 */
#define RUN_ONCE_ALT(once, initalt, init)                               \
    (CRYPTO_THREAD_run_once(once, initalt##_ossl_) ? init##_ossl_ret_ : 0)
