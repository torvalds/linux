/* rep-cache.h : interface to rep cache db functions
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

#ifndef SVN_LIBSVN_FS_FS_REP_CACHE_H
#define SVN_LIBSVN_FS_FS_REP_CACHE_H

#include "svn_error.h"

#include "fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define REP_CACHE_DB_NAME        "rep-cache.db"

/* Open and create, if needed, the rep cache database associated with FS.
   Use POOL for temporary allocations. */
svn_error_t *
svn_fs_fs__open_rep_cache(svn_fs_t *fs,
                          apr_pool_t *pool);

/* Close the rep cache database associated with FS. */
svn_error_t *
svn_fs_fs__close_rep_cache(svn_fs_t *fs);

/* Set *EXISTS to TRUE iff the rep-cache DB file exists. */
svn_error_t *
svn_fs_fs__exists_rep_cache(svn_boolean_t *exists,
                            svn_fs_t *fs, apr_pool_t *pool);

/* Iterate all representations currently in FS's cache. */
svn_error_t *
svn_fs_fs__walk_rep_reference(svn_fs_t *fs,
                              svn_revnum_t start,
                              svn_revnum_t end,
                              svn_error_t *(*walker)(representation_t *rep,
                                                     void *walker_baton,
                                                     svn_fs_t *fs,
                                                     apr_pool_t *scratch_pool),
                              void *walker_baton,
                              svn_cancel_func_t cancel_func,
                              void *cancel_baton,
                              apr_pool_t *pool);

/* Return the representation REP in FS which has fulltext CHECKSUM.
   *REP_P is allocated in POOL.  If the rep cache database has not been
   opened, just set *REP_P to NULL.  Returns SVN_ERR_FS_CORRUPT if
   a reference beyond HEAD is detected. */
svn_error_t *
svn_fs_fs__get_rep_reference(representation_t **rep_p,
                             svn_fs_t *fs,
                             svn_checksum_t *checksum,
                             apr_pool_t *pool);

/* Set the representation REP in FS, using REP->CHECKSUM.
   Use POOL for temporary allocations.  Returns SVN_ERR_FS_CORRUPT if
   an existing reference beyond HEAD is detected.

   If the rep cache database has not been opened, this may be a no op. */
svn_error_t *
svn_fs_fs__set_rep_reference(svn_fs_t *fs,
                             representation_t *rep,
                             apr_pool_t *pool);

/* Delete from the cache all reps corresponding to revisions younger
   than YOUNGEST. */
svn_error_t *
svn_fs_fs__del_rep_reference(svn_fs_t *fs,
                             svn_revnum_t youngest,
                             apr_pool_t *pool);

/* Start a transaction to take an SQLite reserved lock that prevents
   other writes, call BODY, end the transaction, and return what BODY returned.
 */
svn_error_t *
svn_fs_fs__with_rep_cache_lock(svn_fs_t *fs,
                               svn_error_t *(*body)(void *baton,
                                                    apr_pool_t *pool),
                               void *baton,
                               apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_FS_REP_CACHE_H */
