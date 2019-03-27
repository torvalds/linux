/* lock.h : internal interface to lock functions
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

#ifndef SVN_LIBSVN_FS_X_LOCK_H
#define SVN_LIBSVN_FS_X_LOCK_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* These functions implement some of the calls in the FS loader
   library's fs vtables. */

/* See svn_fs_lock(), svn_fs_lock_many(). */
svn_error_t *
svn_fs_x__lock(svn_fs_t *fs,
               apr_hash_t *targets,
               const char *comment,
               svn_boolean_t is_dav_comment,
               apr_time_t expiration_date,
               svn_boolean_t steal_lock,
               svn_fs_lock_callback_t lock_callback,
               void *lock_baton,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool);

/* See svn_fs_generate_lock_token(). */
svn_error_t *
svn_fs_x__generate_lock_token(const char **token,
                              svn_fs_t *fs,
                              apr_pool_t *pool);

/* See svn_fs_unlock(), svn_fs_unlock_many(). */
svn_error_t *
svn_fs_x__unlock(svn_fs_t *fs,
                 apr_hash_t *targets,
                 svn_boolean_t break_lock,
                 svn_fs_lock_callback_t lock_callback,
                 void *lock_baton,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool);

/* See svn_fs_get_lock(). */
svn_error_t *
svn_fs_x__get_lock(svn_lock_t **lock,
                   svn_fs_t *fs,
                   const char *path,
                   apr_pool_t *pool);

/* See svn_fs_get_locks2(). */
svn_error_t *
svn_fs_x__get_locks(svn_fs_t *fs,
                    const char *path,
                    svn_depth_t depth,
                    svn_fs_get_locks_callback_t get_locks_func,
                    void *get_locks_baton,
                    apr_pool_t *scratch_pool);


/* Examine PATH for existing locks, and check whether they can be
   used.  Use SCRATCH_POOL for temporary allocations.

   If no locks are present, return SVN_NO_ERROR.

   If PATH is locked (or contains locks "below" it, when RECURSE is
   set), then verify that:

      1. a username has been supplied to TRAIL->fs's access-context,
         else return SVN_ERR_FS_NO_USER.

      2. for every lock discovered, the current username in the access
         context of TRAIL->fs matches the "owner" of the lock, else
         return SVN_ERR_FS_LOCK_OWNER_MISMATCH.

      3. for every lock discovered, a matching lock token has been
         passed into TRAIL->fs's access-context, else return
         SVN_ERR_FS_BAD_LOCK_TOKEN.

   If all three conditions are met, return SVN_NO_ERROR.

   If the caller (directly or indirectly) has the FS write lock,
   HAVE_WRITE_LOCK should be true.
*/
svn_error_t *
svn_fs_x__allow_locked_operation(const char *path,
                                 svn_fs_t *fs,
                                 svn_boolean_t recurse,
                                 svn_boolean_t have_write_lock,
                                 apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_X_LOCK_H */
