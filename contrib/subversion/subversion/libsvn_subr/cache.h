/*
 * cache.h: cache vtable interface
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_SUBR_CACHE_H
#define SVN_LIBSVN_SUBR_CACHE_H

#include "private/svn_cache.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct svn_cache__vtable_t {
  /* See svn_cache__get(). */
  svn_error_t *(*get)(void **value,
                      svn_boolean_t *found,
                      void *cache_implementation,
                      const void *key,
                      apr_pool_t *result_pool);

  /* See svn_cache__has_key(). */
  svn_error_t *(*has_key)(svn_boolean_t *found,
                          void *cache_implementation,
                          const void *key,
                          apr_pool_t *scratch_pool);

  /* See svn_cache__set(). */
  svn_error_t *(*set)(void *cache_implementation,
                      const void *key,
                      void *value,
                      apr_pool_t *scratch_pool);

  /* See svn_cache__iter(). */
  svn_error_t *(*iter)(svn_boolean_t *completed,
                       void *cache_implementation,
                       svn_iter_apr_hash_cb_t func,
                       void *baton,
                       apr_pool_t *scratch_pool);

  /* See svn_cache__is_cachable(). */
  svn_boolean_t (*is_cachable)(void *cache_implementation,
                               apr_size_t size);

  /* See svn_cache__get_partial(). */
  svn_error_t *(*get_partial)(void **value,
                              svn_boolean_t *found,
                              void *cache_implementation,
                              const void *key,
                              svn_cache__partial_getter_func_t func,
                              void *baton,
                              apr_pool_t *result_pool);

  /* See svn_cache__set_partial(). */
  svn_error_t *(*set_partial)(void *cache_implementation,
                              const void *key,
                              svn_cache__partial_setter_func_t func,
                              void *baton,
                              apr_pool_t *scratch_pool);

  /* See svn_cache__get_info(). */
  svn_error_t *(*get_info)(void *cache_implementation,
                           svn_cache__info_t *info,
                           svn_boolean_t reset,
                           apr_pool_t *result_pool);
} svn_cache__vtable_t;

struct svn_cache__t {
  const svn_cache__vtable_t *vtable;

  /* See svn_cache__set_error_handler(). */
  svn_cache__error_handler_t error_handler;
  void *error_baton;

  /* Private data for the cache implementation. */
  void *cache_internal;

  /* Total number of calls to getters. */
  apr_uint64_t reads;

  /* Total number of calls to set(). */
  apr_uint64_t writes;

  /* Total number of getter calls that returned a cached item. */
  apr_uint64_t hits;

  /* Total number of function calls that returned an error. */
  apr_uint64_t failures;

  /* Cause all getters to act as though the cache contains no data.
     (Currently this never becomes set except in maintainer builds.) */
  svn_boolean_t pretend_empty;
};


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_SUBR_CACHE_H */
