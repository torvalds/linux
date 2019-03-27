/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef APR_POOLS_H
#define APR_POOLS_H

/**
 * @file apr_pools.h
 * @brief APR memory allocation
 *
 * Resource allocation routines...
 *
 * designed so that we don't have to keep track of EVERYTHING so that
 * it can be explicitly freed later (a fundamentally unsound strategy ---
 * particularly in the presence of die()).
 *
 * Instead, we maintain pools, and allocate items (both memory and I/O
 * handlers) from the pools --- currently there are two, one for
 * per-transaction info, and one for config info.  When a transaction is
 * over, we can delete everything in the per-transaction apr_pool_t without
 * fear, and without thinking too hard about it either.
 *
 * Note that most operations on pools are not thread-safe: a single pool
 * should only be accessed by a single thread at any given time. The one
 * exception to this rule is creating a subpool of a given pool: one or more
 * threads can safely create subpools at the same time that another thread
 * accesses the parent pool.
 */

#include "apr.h"
#include "apr_errno.h"
#include "apr_general.h" /* for APR_STRINGIFY */
#define APR_WANT_MEMFUNC /**< for no good reason? */
#include "apr_want.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup apr_pools Memory Pool Functions
 * @ingroup APR 
 * @{
 */

/** The fundamental pool type */
typedef struct apr_pool_t apr_pool_t;


/**
 * Declaration helper macro to construct apr_foo_pool_get()s.
 *
 * This standardized macro is used by opaque (APR) data types to return
 * the apr_pool_t that is associated with the data type.
 *
 * APR_POOL_DECLARE_ACCESSOR() is used in a header file to declare the
 * accessor function. A typical usage and result would be:
 * <pre>
 *    APR_POOL_DECLARE_ACCESSOR(file);
 * becomes:
 *    APR_DECLARE(apr_pool_t *) apr_file_pool_get(const apr_file_t *thefile);
 * </pre>
 * @remark Doxygen unwraps this macro (via doxygen.conf) to provide 
 * actual help for each specific occurrence of apr_foo_pool_get.
 * @remark the linkage is specified for APR. It would be possible to expand
 *       the macros to support other linkages.
 */
#define APR_POOL_DECLARE_ACCESSOR(type) \
    APR_DECLARE(apr_pool_t *) apr_##type##_pool_get \
        (const apr_##type##_t *the##type)

/** 
 * Implementation helper macro to provide apr_foo_pool_get()s.
 *
 * In the implementation, the APR_POOL_IMPLEMENT_ACCESSOR() is used to
 * actually define the function. It assumes the field is named "pool".
 */
#define APR_POOL_IMPLEMENT_ACCESSOR(type) \
    APR_DECLARE(apr_pool_t *) apr_##type##_pool_get \
            (const apr_##type##_t *the##type) \
        { return the##type->pool; }


/**
 * Pool debug levels
 *
 * <pre>
 * | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
 * ---------------------------------
 * |   |   |   |   |   |   |   | x |  General debug code enabled (useful in
 *                                    combination with --with-efence).
 *
 * |   |   |   |   |   |   | x |   |  Verbose output on stderr (report
 *                                    CREATE, CLEAR, DESTROY).
 *
 * |   |   |   | x |   |   |   |   |  Verbose output on stderr (report
 *                                    PALLOC, PCALLOC).
 *
 * |   |   |   |   |   | x |   |   |  Lifetime checking. On each use of a
 *                                    pool, check its lifetime.  If the pool
 *                                    is out of scope, abort().
 *                                    In combination with the verbose flag
 *                                    above, it will output LIFE in such an
 *                                    event prior to aborting.
 *
 * |   |   |   |   | x |   |   |   |  Pool owner checking.  On each use of a
 *                                    pool, check if the current thread is the
 *                                    pool's owner.  If not, abort().  In
 *                                    combination with the verbose flag above,
 *                                    it will output OWNER in such an event
 *                                    prior to aborting.  Use the debug
 *                                    function apr_pool_owner_set() to switch
 *                                    a pool's ownership.
 *
 * When no debug level was specified, assume general debug mode.
 * If level 0 was specified, debugging is switched off.
 * </pre>
 */
#if defined(APR_POOL_DEBUG)
/* If APR_POOL_DEBUG is blank, we get 1; if it is a number, we get -1. */
#if (APR_POOL_DEBUG - APR_POOL_DEBUG -1 == 1)
#undef APR_POOL_DEBUG
#define APR_POOL_DEBUG 1
#endif
#else
#define APR_POOL_DEBUG 0
#endif

/** the place in the code where the particular function was called */
#define APR_POOL__FILE_LINE__ __FILE__ ":" APR_STRINGIFY(__LINE__)



/** A function that is called when allocation fails. */
typedef int (*apr_abortfunc_t)(int retcode);

/*
 * APR memory structure manipulators (pools, tables, and arrays).
 */

/*
 * Initialization
 */

/**
 * Setup all of the internal structures required to use pools
 * @remark Programs do NOT need to call this directly.  APR will call this
 *      automatically from apr_initialize.
 * @internal
 */
APR_DECLARE(apr_status_t) apr_pool_initialize(void);

/**
 * Tear down all of the internal structures required to use pools
 * @remark Programs do NOT need to call this directly.  APR will call this
 *      automatically from apr_terminate.
 * @internal
 */
APR_DECLARE(void) apr_pool_terminate(void);


/*
 * Pool creation/destruction
 */

#include "apr_allocator.h"

/**
 * Create a new pool.
 * @param newpool The pool we have just created.
 * @param parent The parent pool.  If this is NULL, the new pool is a root
 *        pool.  If it is non-NULL, the new pool will inherit all
 *        of its parent pool's attributes, except the apr_pool_t will
 *        be a sub-pool.
 * @param abort_fn A function to use if the pool cannot allocate more memory.
 * @param allocator The allocator to use with the new pool.  If NULL the
 *        allocator of the parent pool will be used.
 * @remark This function is thread-safe, in the sense that multiple threads
 *         can safely create subpools of the same parent pool concurrently.
 *         Similarly, a subpool can be created by one thread at the same
 *         time that another thread accesses the parent pool.
 */
APR_DECLARE(apr_status_t) apr_pool_create_ex(apr_pool_t **newpool,
                                             apr_pool_t *parent,
                                             apr_abortfunc_t abort_fn,
                                             apr_allocator_t *allocator)
                          __attribute__((nonnull(1)));

/**
 * Create a new pool.
 * @deprecated @see apr_pool_create_unmanaged_ex.
 */
APR_DECLARE(apr_status_t) apr_pool_create_core_ex(apr_pool_t **newpool,
                                                  apr_abortfunc_t abort_fn,
                                                  apr_allocator_t *allocator);

/**
 * Create a new unmanaged pool.
 * @param newpool The pool we have just created.
 * @param abort_fn A function to use if the pool cannot allocate more memory.
 * @param allocator The allocator to use with the new pool.  If NULL a
 *        new allocator will be created with the new pool as owner.
 * @remark An unmanaged pool is a special pool without a parent; it will
 *         NOT be destroyed upon apr_terminate.  It must be explicitly
 *         destroyed by calling apr_pool_destroy, to prevent memory leaks.
 *         Use of this function is discouraged, think twice about whether
 *         you really really need it.
 * @warning Any child cleanups registered against the new pool, or
 *         against sub-pools thereof, will not be executed during an
 *         invocation of apr_proc_create(), so resources created in an
 *         "unmanaged" pool hierarchy will leak to child processes.
 */
APR_DECLARE(apr_status_t) apr_pool_create_unmanaged_ex(apr_pool_t **newpool,
                                                   apr_abortfunc_t abort_fn,
                                                   apr_allocator_t *allocator)
                          __attribute__((nonnull(1)));

/**
 * Debug version of apr_pool_create_ex.
 * @param newpool @see apr_pool_create.
 * @param parent @see apr_pool_create.
 * @param abort_fn @see apr_pool_create.
 * @param allocator @see apr_pool_create.
 * @param file_line Where the function is called from.
 *        This is usually APR_POOL__FILE_LINE__.
 * @remark Only available when APR_POOL_DEBUG is defined.
 *         Call this directly if you have your apr_pool_create_ex
 *         calls in a wrapper function and wish to override
 *         the file_line argument to reflect the caller of
 *         your wrapper function.  If you do not have
 *         apr_pool_create_ex in a wrapper, trust the macro
 *         and don't call apr_pool_create_ex_debug directly.
 */
APR_DECLARE(apr_status_t) apr_pool_create_ex_debug(apr_pool_t **newpool,
                                                   apr_pool_t *parent,
                                                   apr_abortfunc_t abort_fn,
                                                   apr_allocator_t *allocator,
                                                   const char *file_line)
                          __attribute__((nonnull(1)));

#if APR_POOL_DEBUG
#define apr_pool_create_ex(newpool, parent, abort_fn, allocator)  \
    apr_pool_create_ex_debug(newpool, parent, abort_fn, allocator, \
                             APR_POOL__FILE_LINE__)
#endif

/**
 * Debug version of apr_pool_create_core_ex.
 * @deprecated @see apr_pool_create_unmanaged_ex_debug.
 */
APR_DECLARE(apr_status_t) apr_pool_create_core_ex_debug(apr_pool_t **newpool,
                                                   apr_abortfunc_t abort_fn,
                                                   apr_allocator_t *allocator,
                                                   const char *file_line);

/**
 * Debug version of apr_pool_create_unmanaged_ex.
 * @param newpool @see apr_pool_create_unmanaged.
 * @param abort_fn @see apr_pool_create_unmanaged.
 * @param allocator @see apr_pool_create_unmanaged.
 * @param file_line Where the function is called from.
 *        This is usually APR_POOL__FILE_LINE__.
 * @remark Only available when APR_POOL_DEBUG is defined.
 *         Call this directly if you have your apr_pool_create_unmanaged_ex
 *         calls in a wrapper function and wish to override
 *         the file_line argument to reflect the caller of
 *         your wrapper function.  If you do not have
 *         apr_pool_create_core_ex in a wrapper, trust the macro
 *         and don't call apr_pool_create_core_ex_debug directly.
 */
APR_DECLARE(apr_status_t) apr_pool_create_unmanaged_ex_debug(apr_pool_t **newpool,
                                                   apr_abortfunc_t abort_fn,
                                                   apr_allocator_t *allocator,
                                                   const char *file_line)
                          __attribute__((nonnull(1)));

#if APR_POOL_DEBUG
#define apr_pool_create_core_ex(newpool, abort_fn, allocator)  \
    apr_pool_create_unmanaged_ex_debug(newpool, abort_fn, allocator, \
                                  APR_POOL__FILE_LINE__)

#define apr_pool_create_unmanaged_ex(newpool, abort_fn, allocator)  \
    apr_pool_create_unmanaged_ex_debug(newpool, abort_fn, allocator, \
                                  APR_POOL__FILE_LINE__)

#endif

/**
 * Create a new pool.
 * @param newpool The pool we have just created.
 * @param parent The parent pool.  If this is NULL, the new pool is a root
 *        pool.  If it is non-NULL, the new pool will inherit all
 *        of its parent pool's attributes, except the apr_pool_t will
 *        be a sub-pool.
 * @remark This function is thread-safe, in the sense that multiple threads
 *         can safely create subpools of the same parent pool concurrently.
 *         Similarly, a subpool can be created by one thread at the same
 *         time that another thread accesses the parent pool.
 */
#if defined(DOXYGEN)
APR_DECLARE(apr_status_t) apr_pool_create(apr_pool_t **newpool,
                                          apr_pool_t *parent);
#else
#if APR_POOL_DEBUG
#define apr_pool_create(newpool, parent) \
    apr_pool_create_ex_debug(newpool, parent, NULL, NULL, \
                             APR_POOL__FILE_LINE__)
#else
#define apr_pool_create(newpool, parent) \
    apr_pool_create_ex(newpool, parent, NULL, NULL)
#endif
#endif

/**
 * Create a new unmanaged pool.
 * @param newpool The pool we have just created.
 */
#if defined(DOXYGEN)
APR_DECLARE(apr_status_t) apr_pool_create_core(apr_pool_t **newpool);
APR_DECLARE(apr_status_t) apr_pool_create_unmanaged(apr_pool_t **newpool);
#else
#if APR_POOL_DEBUG
#define apr_pool_create_core(newpool) \
    apr_pool_create_unmanaged_ex_debug(newpool, NULL, NULL, \
                                  APR_POOL__FILE_LINE__)
#define apr_pool_create_unmanaged(newpool) \
    apr_pool_create_unmanaged_ex_debug(newpool, NULL, NULL, \
                                  APR_POOL__FILE_LINE__)
#else
#define apr_pool_create_core(newpool) \
    apr_pool_create_unmanaged_ex(newpool, NULL, NULL)
#define apr_pool_create_unmanaged(newpool) \
    apr_pool_create_unmanaged_ex(newpool, NULL, NULL)
#endif
#endif

/**
 * Find the pool's allocator
 * @param pool The pool to get the allocator from.
 */
APR_DECLARE(apr_allocator_t *) apr_pool_allocator_get(apr_pool_t *pool)
                               __attribute__((nonnull(1)));

/**
 * Clear all memory in the pool and run all the cleanups. This also destroys all
 * subpools.
 * @param p The pool to clear
 * @remark This does not actually free the memory, it just allows the pool
 *         to re-use this memory for the next allocation.
 * @see apr_pool_destroy()
 */
APR_DECLARE(void) apr_pool_clear(apr_pool_t *p) __attribute__((nonnull(1)));

/**
 * Debug version of apr_pool_clear.
 * @param p See: apr_pool_clear.
 * @param file_line Where the function is called from.
 *        This is usually APR_POOL__FILE_LINE__.
 * @remark Only available when APR_POOL_DEBUG is defined.
 *         Call this directly if you have your apr_pool_clear
 *         calls in a wrapper function and wish to override
 *         the file_line argument to reflect the caller of
 *         your wrapper function.  If you do not have
 *         apr_pool_clear in a wrapper, trust the macro
 *         and don't call apr_pool_destroy_clear directly.
 */
APR_DECLARE(void) apr_pool_clear_debug(apr_pool_t *p,
                                       const char *file_line)
                  __attribute__((nonnull(1)));

#if APR_POOL_DEBUG
#define apr_pool_clear(p) \
    apr_pool_clear_debug(p, APR_POOL__FILE_LINE__)
#endif

/**
 * Destroy the pool. This takes similar action as apr_pool_clear() and then
 * frees all the memory.
 * @param p The pool to destroy
 * @remark This will actually free the memory
 */
APR_DECLARE(void) apr_pool_destroy(apr_pool_t *p) __attribute__((nonnull(1)));

/**
 * Debug version of apr_pool_destroy.
 * @param p See: apr_pool_destroy.
 * @param file_line Where the function is called from.
 *        This is usually APR_POOL__FILE_LINE__.
 * @remark Only available when APR_POOL_DEBUG is defined.
 *         Call this directly if you have your apr_pool_destroy
 *         calls in a wrapper function and wish to override
 *         the file_line argument to reflect the caller of
 *         your wrapper function.  If you do not have
 *         apr_pool_destroy in a wrapper, trust the macro
 *         and don't call apr_pool_destroy_debug directly.
 */
APR_DECLARE(void) apr_pool_destroy_debug(apr_pool_t *p,
                                         const char *file_line)
                  __attribute__((nonnull(1)));

#if APR_POOL_DEBUG
#define apr_pool_destroy(p) \
    apr_pool_destroy_debug(p, APR_POOL__FILE_LINE__)
#endif


/*
 * Memory allocation
 */

/**
 * Allocate a block of memory from a pool
 * @param p The pool to allocate from
 * @param size The amount of memory to allocate
 * @return The allocated memory
 */
APR_DECLARE(void *) apr_palloc(apr_pool_t *p, apr_size_t size)
#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 4))
                    __attribute__((alloc_size(2)))
#endif
                    __attribute__((nonnull(1)));

/**
 * Debug version of apr_palloc
 * @param p See: apr_palloc
 * @param size See: apr_palloc
 * @param file_line Where the function is called from.
 *        This is usually APR_POOL__FILE_LINE__.
 * @return See: apr_palloc
 */
APR_DECLARE(void *) apr_palloc_debug(apr_pool_t *p, apr_size_t size,
                                     const char *file_line)
#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 4))
                    __attribute__((alloc_size(2)))
#endif
                    __attribute__((nonnull(1)));

#if APR_POOL_DEBUG
#define apr_palloc(p, size) \
    apr_palloc_debug(p, size, APR_POOL__FILE_LINE__)
#endif

/**
 * Allocate a block of memory from a pool and set all of the memory to 0
 * @param p The pool to allocate from
 * @param size The amount of memory to allocate
 * @return The allocated memory
 */
#if defined(DOXYGEN)
APR_DECLARE(void *) apr_pcalloc(apr_pool_t *p, apr_size_t size);
#elif !APR_POOL_DEBUG
#define apr_pcalloc(p, size) memset(apr_palloc(p, size), 0, size)
#endif

/**
 * Debug version of apr_pcalloc
 * @param p See: apr_pcalloc
 * @param size See: apr_pcalloc
 * @param file_line Where the function is called from.
 *        This is usually APR_POOL__FILE_LINE__.
 * @return See: apr_pcalloc
 */
APR_DECLARE(void *) apr_pcalloc_debug(apr_pool_t *p, apr_size_t size,
                                      const char *file_line)
                    __attribute__((nonnull(1)));

#if APR_POOL_DEBUG
#define apr_pcalloc(p, size) \
    apr_pcalloc_debug(p, size, APR_POOL__FILE_LINE__)
#endif


/*
 * Pool Properties
 */

/**
 * Set the function to be called when an allocation failure occurs.
 * @remark If the program wants APR to exit on a memory allocation error,
 *      then this function can be called to set the callback to use (for
 *      performing cleanup and then exiting). If this function is not called,
 *      then APR will return an error and expect the calling program to
 *      deal with the error accordingly.
 */
APR_DECLARE(void) apr_pool_abort_set(apr_abortfunc_t abortfunc,
                                     apr_pool_t *pool)
                  __attribute__((nonnull(2)));

/**
 * Get the abort function associated with the specified pool.
 * @param pool The pool for retrieving the abort function.
 * @return The abort function for the given pool.
 */
APR_DECLARE(apr_abortfunc_t) apr_pool_abort_get(apr_pool_t *pool)
                             __attribute__((nonnull(1)));

/**
 * Get the parent pool of the specified pool.
 * @param pool The pool for retrieving the parent pool.
 * @return The parent of the given pool.
 */
APR_DECLARE(apr_pool_t *) apr_pool_parent_get(apr_pool_t *pool)
                          __attribute__((nonnull(1)));

/**
 * Determine if pool a is an ancestor of pool b.
 * @param a The pool to search
 * @param b The pool to search for
 * @return True if a is an ancestor of b, NULL is considered an ancestor
 *         of all pools.
 * @remark if compiled with APR_POOL_DEBUG, this function will also
 * return true if A is a pool which has been guaranteed by the caller
 * (using apr_pool_join) to have a lifetime at least as long as some
 * ancestor of pool B.
 */
APR_DECLARE(int) apr_pool_is_ancestor(apr_pool_t *a, apr_pool_t *b);

/**
 * Tag a pool (give it a name)
 * @param pool The pool to tag
 * @param tag  The tag
 */
APR_DECLARE(void) apr_pool_tag(apr_pool_t *pool, const char *tag)
                  __attribute__((nonnull(1)));


/*
 * User data management
 */

/**
 * Set the data associated with the current pool
 * @param data The user data associated with the pool.
 * @param key The key to use for association
 * @param cleanup The cleanup program to use to cleanup the data (NULL if none)
 * @param pool The current pool
 * @warning The data to be attached to the pool should have a life span
 *          at least as long as the pool it is being attached to.
 *
 *      Users of APR must take EXTREME care when choosing a key to
 *      use for their data.  It is possible to accidentally overwrite
 *      data by choosing a key that another part of the program is using.
 *      Therefore it is advised that steps are taken to ensure that unique
 *      keys are used for all of the userdata objects in a particular pool
 *      (the same key in two different pools or a pool and one of its
 *      subpools is okay) at all times.  Careful namespace prefixing of
 *      key names is a typical way to help ensure this uniqueness.
 *
 */
APR_DECLARE(apr_status_t) apr_pool_userdata_set(const void *data,
                                                const char *key,
                                                apr_status_t (*cleanup)(void *),
                                                apr_pool_t *pool)
                          __attribute__((nonnull(2,4)));

/**
 * Set the data associated with the current pool
 * @param data The user data associated with the pool.
 * @param key The key to use for association
 * @param cleanup The cleanup program to use to cleanup the data (NULL if none)
 * @param pool The current pool
 * @note same as apr_pool_userdata_set(), except that this version doesn't
 *       make a copy of the key (this function is useful, for example, when
 *       the key is a string literal)
 * @warning This should NOT be used if the key could change addresses by
 *       any means between the apr_pool_userdata_setn() call and a
 *       subsequent apr_pool_userdata_get() on that key, such as if a
 *       static string is used as a userdata key in a DSO and the DSO could
 *       be unloaded and reloaded between the _setn() and the _get().  You
 *       MUST use apr_pool_userdata_set() in such cases.
 * @warning More generally, the key and the data to be attached to the
 *       pool should have a life span at least as long as the pool itself.
 *
 */
APR_DECLARE(apr_status_t) apr_pool_userdata_setn(
                                const void *data, const char *key,
                                apr_status_t (*cleanup)(void *),
                                apr_pool_t *pool)
                          __attribute__((nonnull(2,4)));

/**
 * Return the data associated with the current pool.
 * @param data The user data associated with the pool.
 * @param key The key for the data to retrieve
 * @param pool The current pool.
 */
APR_DECLARE(apr_status_t) apr_pool_userdata_get(void **data, const char *key,
                                                apr_pool_t *pool)
                          __attribute__((nonnull(1,2,3)));


/**
 * @defgroup PoolCleanup  Pool Cleanup Functions
 *
 * Cleanups are performed in the reverse order they were registered.  That is:
 * Last In, First Out.  A cleanup function can safely allocate memory from
 * the pool that is being cleaned up. It can also safely register additional
 * cleanups which will be run LIFO, directly after the current cleanup
 * terminates.  Cleanups have to take caution in calling functions that
 * create subpools. Subpools, created during cleanup will NOT automatically
 * be cleaned up.  In other words, cleanups are to clean up after themselves.
 *
 * @{
 */

/**
 * Register a function to be called when a pool is cleared or destroyed
 * @param p The pool to register the cleanup with
 * @param data The data to pass to the cleanup function.
 * @param plain_cleanup The function to call when the pool is cleared
 *                      or destroyed
 * @param child_cleanup The function to call when a child process is about
 *                      to exec - this function is called in the child, obviously!
 */
APR_DECLARE(void) apr_pool_cleanup_register(
                            apr_pool_t *p, const void *data,
                            apr_status_t (*plain_cleanup)(void *),
                            apr_status_t (*child_cleanup)(void *))
                  __attribute__((nonnull(3,4)));

/**
 * Register a function to be called when a pool is cleared or destroyed.
 *
 * Unlike apr_pool_cleanup_register which registers a cleanup
 * that is called AFTER all subpools are destroyed, this function registers
 * a function that will be called before any of the subpools are destroyed.
 *
 * @param p The pool to register the cleanup with
 * @param data The data to pass to the cleanup function.
 * @param plain_cleanup The function to call when the pool is cleared
 *                      or destroyed
 */
APR_DECLARE(void) apr_pool_pre_cleanup_register(
                            apr_pool_t *p, const void *data,
                            apr_status_t (*plain_cleanup)(void *))
                  __attribute__((nonnull(3)));

/**
 * Remove a previously registered cleanup function.
 * 
 * The cleanup most recently registered with @a p having the same values of
 * @a data and @a cleanup will be removed.
 *
 * @param p The pool to remove the cleanup from
 * @param data The data of the registered cleanup
 * @param cleanup The function to remove from cleanup
 * @remarks For some strange reason only the plain_cleanup is handled by this
 *          function
 */
APR_DECLARE(void) apr_pool_cleanup_kill(apr_pool_t *p, const void *data,
                                        apr_status_t (*cleanup)(void *))
                  __attribute__((nonnull(3)));

/**
 * Replace the child cleanup function of a previously registered cleanup.
 * 
 * The cleanup most recently registered with @a p having the same values of
 * @a data and @a plain_cleanup will have the registered child cleanup
 * function replaced with @a child_cleanup.
 *
 * @param p The pool of the registered cleanup
 * @param data The data of the registered cleanup
 * @param plain_cleanup The plain cleanup function of the registered cleanup
 * @param child_cleanup The function to register as the child cleanup
 */
APR_DECLARE(void) apr_pool_child_cleanup_set(
                        apr_pool_t *p, const void *data,
                        apr_status_t (*plain_cleanup)(void *),
                        apr_status_t (*child_cleanup)(void *))
                  __attribute__((nonnull(3,4)));

/**
 * Run the specified cleanup function immediately and unregister it.
 *
 * The cleanup most recently registered with @a p having the same values of
 * @a data and @a cleanup will be removed and @a cleanup will be called
 * with @a data as the argument.
 *
 * @param p The pool to remove the cleanup from
 * @param data The data to remove from cleanup
 * @param cleanup The function to remove from cleanup
 */
APR_DECLARE(apr_status_t) apr_pool_cleanup_run(apr_pool_t *p, void *data,
                                               apr_status_t (*cleanup)(void *))
                          __attribute__((nonnull(3)));

/**
 * An empty cleanup function.
 * 
 * Passed to apr_pool_cleanup_register() when no cleanup is required.
 *
 * @param data The data to cleanup, will not be used by this function.
 */
APR_DECLARE_NONSTD(apr_status_t) apr_pool_cleanup_null(void *data);

/**
 * Run all registered child cleanups, in preparation for an exec()
 * call in a forked child -- close files, etc., but *don't* flush I/O
 * buffers, *don't* wait for subprocesses, and *don't* free any
 * memory.
 */
APR_DECLARE(void) apr_pool_cleanup_for_exec(void);

/** @} */

/**
 * @defgroup PoolDebug Pool Debugging functions.
 *
 * pools have nested lifetimes -- sub_pools are destroyed when the
 * parent pool is cleared.  We allow certain liberties with operations
 * on things such as tables (and on other structures in a more general
 * sense) where we allow the caller to insert values into a table which
 * were not allocated from the table's pool.  The table's data will
 * remain valid as long as all the pools from which its values are
 * allocated remain valid.
 *
 * For example, if B is a sub pool of A, and you build a table T in
 * pool B, then it's safe to insert data allocated in A or B into T
 * (because B lives at most as long as A does, and T is destroyed when
 * B is cleared/destroyed).  On the other hand, if S is a table in
 * pool A, it is safe to insert data allocated in A into S, but it
 * is *not safe* to insert data allocated from B into S... because
 * B can be cleared/destroyed before A is (which would leave dangling
 * pointers in T's data structures).
 *
 * In general we say that it is safe to insert data into a table T
 * if the data is allocated in any ancestor of T's pool.  This is the
 * basis on which the APR_POOL_DEBUG code works -- it tests these ancestor
 * relationships for all data inserted into tables.  APR_POOL_DEBUG also
 * provides tools (apr_pool_find, and apr_pool_is_ancestor) for other
 * folks to implement similar restrictions for their own data
 * structures.
 *
 * However, sometimes this ancestor requirement is inconvenient --
 * sometimes it's necessary to create a sub pool where the sub pool is
 * guaranteed to have the same lifetime as the parent pool.  This is a
 * guarantee implemented by the *caller*, not by the pool code.  That
 * is, the caller guarantees they won't destroy the sub pool
 * individually prior to destroying the parent pool.
 *
 * In this case the caller must call apr_pool_join() to indicate this
 * guarantee to the APR_POOL_DEBUG code.
 *
 * These functions are only implemented when #APR_POOL_DEBUG is set.
 *
 * @{
 */
#if APR_POOL_DEBUG || defined(DOXYGEN)
/**
 * Guarantee that a subpool has the same lifetime as the parent.
 * @param p The parent pool
 * @param sub The subpool
 */
APR_DECLARE(void) apr_pool_join(apr_pool_t *p, apr_pool_t *sub)
                  __attribute__((nonnull(2)));

/**
 * Find a pool from something allocated in it.
 * @param mem The thing allocated in the pool
 * @return The pool it is allocated in
 */
APR_DECLARE(apr_pool_t *) apr_pool_find(const void *mem);

/**
 * Report the number of bytes currently in the pool
 * @param p The pool to inspect
 * @param recurse Recurse/include the subpools' sizes
 * @return The number of bytes
 */
APR_DECLARE(apr_size_t) apr_pool_num_bytes(apr_pool_t *p, int recurse)
                        __attribute__((nonnull(1)));

/**
 * Lock a pool
 * @param pool The pool to lock
 * @param flag  The flag
 */
APR_DECLARE(void) apr_pool_lock(apr_pool_t *pool, int flag);

/* @} */

#else /* APR_POOL_DEBUG or DOXYGEN */

#ifdef apr_pool_join
#undef apr_pool_join
#endif
#define apr_pool_join(a,b)

#ifdef apr_pool_lock
#undef apr_pool_lock
#endif
#define apr_pool_lock(pool, lock)

#endif /* APR_POOL_DEBUG or DOXYGEN */

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* !APR_POOLS_H */
