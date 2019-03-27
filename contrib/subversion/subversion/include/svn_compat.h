/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_compat.h
 * @brief Utilities to help applications provide backwards-compatibility
 */

#ifndef SVN_COMPAT_H
#define SVN_COMPAT_H

#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>

#include "svn_types.h"
#include "svn_string.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Return, in @a *callback2 and @a *callback2_baton a function/baton that
 * will call @a callback/@a callback_baton, allocating the @a *callback2_baton
 * in @a pool.
 *
 * @note This is used by compatibility wrappers, which exist in more than
 * Subversion core library.
 *
 * @since New in 1.4.
 */
void
svn_compat_wrap_commit_callback(svn_commit_callback2_t *callback2,
                                void **callback2_baton,
                                svn_commit_callback_t callback,
                                void *callback_baton,
                                apr_pool_t *pool);

/** Clear svn:author, svn:date, and svn:log from @a revprops if not NULL.
 * Use this if you must handle these three properties separately for
 * compatibility reasons.
 *
 * @since New in 1.5.
 */
void
svn_compat_log_revprops_clear(apr_hash_t *revprops);

/** Return a list to pass to post-1.5 log-retrieval functions in order to
 * retrieve the pre-1.5 set of revprops: svn:author, svn:date, and svn:log.
 *
 * @since New in 1.5.
 */
apr_array_header_t *
svn_compat_log_revprops_in(apr_pool_t *pool);

/** Return, in @a **author, @a **date, and @a **message, the values of the
 * svn:author, svn:date, and svn:log revprops from @a revprops.  If @a
 * revprops is NULL, all return values are NULL.  Any return value may be
 * NULL if the corresponding property is not set in @a revprops.
 *
 * @since New in 1.9.
 */
void
svn_compat_log_revprops_out_string(const svn_string_t **author,
                                   const svn_string_t **date,
                                   const svn_string_t **message,
                                   apr_hash_t *revprops);

/** Simiar to svn_compat_log_revprops_out_string() but returns C-style strings
 * instead of #svn_string_t.
 *
 * @since New in 1.5.
 */
void
svn_compat_log_revprops_out(const char **author, const char **date,
                            const char **message, apr_hash_t *revprops);

/** Return, in @a *receiver2 and @a *receiver2_baton a function/baton that
 * will call @a receiver/@a receiver_baton, allocating the @a *receiver2_baton
 * in @a pool.
 *
 * @note This is used by compatibility wrappers, which exist in more than
 * Subversion core library.
 *
 * @since New in 1.5.
 */
void
svn_compat_wrap_log_receiver(svn_log_entry_receiver_t *receiver2,
                             void **receiver2_baton,
                             svn_log_message_receiver_t receiver,
                             void *receiver_baton,
                             apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_COMPAT_H */
