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
 * @file svn_cache_config.h
 * @brief Configuration interface to internal Subversion caches.
 */

#ifndef SVN_CACHE_CONFIG_H
#define SVN_CACHE_CONFIG_H

#include <apr.h>
#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** @defgroup svn_fs_cache_config caching configuration
 * @{
 * @since New in 1.7. */

/** Cache resource settings. It controls what caches, in what size and
   how they will be created. The settings apply for the whole process.

   @note Do not extend this data structure as this would break binary
         compatibility.

   @since New in 1.7.
 */
typedef struct svn_cache_config_t
{
  /** total cache size in bytes. Please note that this is only soft limit
     to the total application memory usage and will be exceeded due to
     temporary objects and other program state.
     May be 0, resulting in default caching code being used. */
  apr_uint64_t cache_size;

  /** maximum number of files kept open */
  apr_size_t file_handle_count;

  /** is this application guaranteed to be single-threaded? */
  svn_boolean_t single_threaded;

  /* DON'T add new members here.  Bump struct and API version instead. */
} svn_cache_config_t;

/** Get the current cache configuration. If it has not been set,
   this function will return the default settings.

   @since New in 1.7.
 */
const svn_cache_config_t *
svn_cache_config_get(void);

/** Set the cache configuration. Please note that it may not change
   the actual configuration *in use*. Therefore, call it before reading
   data from any repo and call it only once.

   This function is not thread-safe. Therefore, it should be called
   from the processes' initialization code only.

   @since New in 1.7.
 */
void
svn_cache_config_set(const svn_cache_config_t *settings);

/** @} */

/** @} */


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CACHE_CONFIG_H */
