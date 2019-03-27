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
 * @file svn_pools.h
 * @brief APR pool management for Subversion
 */




#ifndef SVN_POOLS_H
#define SVN_POOLS_H

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Wrappers around APR pools, so we get debugging. */

/** The recommended maximum amount of memory (4MB) to keep in an APR
 * allocator on the free list, conveniently defined here to share
 * between all our applications.
 */
#define SVN_ALLOCATOR_RECOMMENDED_MAX_FREE (4096 * 1024)


/** Wrapper around apr_pool_create_ex(), with a simpler interface.
 * The return pool will have an abort function set, which will call
 * abort() on OOM.
 */
apr_pool_t *
svn_pool_create_ex(apr_pool_t *parent_pool,
                   apr_allocator_t *allocator);

#ifndef DOXYGEN_SHOULD_SKIP_THIS
apr_pool_t *
svn_pool_create_ex_debug(apr_pool_t *parent_pool,
                         apr_allocator_t *allocator,
                         const char *file_line);

#if APR_POOL_DEBUG
#define svn_pool_create_ex(pool, allocator) \
svn_pool_create_ex_debug(pool, allocator, APR_POOL__FILE_LINE__)

#endif /* APR_POOL_DEBUG */
#endif /* DOXYGEN_SHOULD_SKIP_THIS */


/** Create a pool as a subpool of @a parent_pool */
#define svn_pool_create(parent_pool) svn_pool_create_ex(parent_pool, NULL)

/** Clear a @a pool destroying its children.
 *
 * This define for @c svn_pool_clear exists for completeness.
 */
#define svn_pool_clear apr_pool_clear


/** Destroy a @a pool and all of its children.
 *
 * This define for @c svn_pool_destroy exists for symmetry and
 * completeness.
 */
#define svn_pool_destroy apr_pool_destroy

/** Return a new allocator.  This function limits the unused memory in the
 * new allocator to #SVN_ALLOCATOR_RECOMMENDED_MAX_FREE and ensures
 * proper synchronization if the allocator is used by multiple threads.
 *
 * If your application uses multiple threads, creating a separate
 * allocator for each of these threads may not be feasible.  Set the
 * @a thread_safe parameter to @c TRUE in that case; otherwise, set @a
 * thread_safe to @c FALSE to maximize performance.
 *
 * @note Even if @a thread_safe is @c TRUE, pools themselves will
 * still not be thread-safe and their access may require explicit
 * serialization.
 *
 * To access the owner pool, which can also serve as the root pool for
 * your sub-pools, call @c apr_allocator_get_owner().
 *
 * @since: New in 1.8
 */
apr_allocator_t *
svn_pool_create_allocator(svn_boolean_t thread_safe);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_POOLS_H */
