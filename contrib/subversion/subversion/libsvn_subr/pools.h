/*
 * pools.h: private pool functions
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

#ifndef SVN_LIBSVN_SUBR_POOLS_H
#define SVN_LIBSVN_SUBR_POOLS_H

#include "svn_pools.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Create an unmanaged, global pool with a new allocator.
   THREAD_SAFE indicates whether the pool's allocator should be
   thread-safe or not. */
apr_pool_t *
svn_pool__create_unmanaged(svn_boolean_t thread_safe);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_SUBR_POOLS_H */
