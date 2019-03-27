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
 * @file svn_user.h
 * @brief Subversion's wrapper around APR's user information interface.
 */

#ifndef SVN_USER_H
#define SVN_USER_H

#include <apr_pools.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Get the name of the current user, using @a pool for any necessary
 * allocation, returning NULL on error.
 *
 * @since New in 1.4.
 */
const char *
svn_user_get_name(apr_pool_t *pool);

/** Get the path of the current user's home directory, using @a pool for
 * any necessary allocation, returning NULL on error.
 *
 * @since New in 1.4.
 * @since 1.10 returns a canonical path. Earlier versions returned a
 * non-canonical path if the operating system reported a non-canonical
 * path such as "/home/user/" or "//home/user".
 */
const char *
svn_user_get_homedir(apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_USER_H */
