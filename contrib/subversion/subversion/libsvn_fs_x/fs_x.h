/* fs_x.h : interface to the FSX layer
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

#ifndef SVN_LIBSVN_FS_X_FS_X_H
#define SVN_LIBSVN_FS_X_FS_X_H

#include "fs.h"

/* Read the 'format' file of fsx filesystem FS and store its info in FS.
 * Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_fs_x__read_format_file(svn_fs_t *fs,
                           apr_pool_t *scratch_pool);

/* Open the fsx filesystem pointed to by PATH and associate it with
   filesystem object FS.  Use SCRATCH_POOL for temporary allocations.

   ### Some parts of *FS must have been initialized beforehand; some parts
       (including FS->path) are initialized by this function. */
svn_error_t *
svn_fs_x__open(svn_fs_t *fs,
               const char *path,
               apr_pool_t *scratch_pool);

/* Initialize parts of the FS data that are being shared across multiple
   filesystem objects.  Use COMMON_POOL for process-wide and SCRATCH_POOL
   for temporary allocations.  Use COMMON_POOL_LOCK to ensure that the
   initialization is serialized. */
svn_error_t *
svn_fs_x__initialize_shared_data(svn_fs_t *fs,
                                 svn_mutex__t *common_pool_lock,
                                 apr_pool_t *scratch_pool,
                                 apr_pool_t *common_pool);

/* Upgrade the fsx filesystem FS.  Indicate progress via the optional
 * NOTIFY_FUNC callback using NOTIFY_BATON.  The optional CANCEL_FUNC
 * will periodically be called with CANCEL_BATON to allow for preemption.
 * Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_fs_x__upgrade(svn_fs_t *fs,
                  svn_fs_upgrade_notify_t notify_func,
                  void *notify_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *scratch_pool);

/* Set *YOUNGEST to the youngest revision in filesystem FS.  Do any
   temporary allocation in SCRATCH_POOL. */
svn_error_t *
svn_fs_x__youngest_rev(svn_revnum_t *youngest,
                       svn_fs_t *fs,
                       apr_pool_t *scratch_pool);

/* Return SVN_ERR_FS_NO_SUCH_REVISION if the given revision REV is newer
   than the current youngest revision in FS or is simply not a valid
   revision number, else return success.  Use SCRATCH_POOL for temporary
   allocations. */
svn_error_t *
svn_fs_x__ensure_revision_exists(svn_revnum_t rev,
                                 svn_fs_t *fs,
                                 apr_pool_t *scratch_pool);

/* Set *LENGTH to the be fulltext length of the node revision
   specified by NODEREV. */
svn_error_t *
svn_fs_x__file_length(svn_filesize_t *length,
                      svn_fs_x__noderev_t *noderev);

/* Return TRUE if the representations in A and B have equal contents, else
   return FALSE. */
svn_boolean_t
svn_fs_x__file_text_rep_equal(svn_fs_x__representation_t *a,
                              svn_fs_x__representation_t *b);

/* Set *EQUAL to TRUE if the property representations in A and B within FS
   have equal contents, else set it to FALSE.  If STRICT is not set, allow
   for false negatives.
   Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_fs_x__prop_rep_equal(svn_boolean_t *equal,
                         svn_fs_t *fs,
                         svn_fs_x__noderev_t *a,
                         svn_fs_x__noderev_t *b,
                         svn_boolean_t strict,
                         apr_pool_t *scratch_pool);


/* Return a copy of the representation REP allocated from RESULT_POOL. */
svn_fs_x__representation_t *
svn_fs_x__rep_copy(svn_fs_x__representation_t *rep,
                   apr_pool_t *result_pool);


/* Return the recorded checksum of type KIND for the text representation
   of NODREV into CHECKSUM, allocating from RESULT_POOL.  If no stored
   checksum is available, put all NULL into CHECKSUM. */
svn_error_t *
svn_fs_x__file_checksum(svn_checksum_t **checksum,
                        svn_fs_x__noderev_t *noderev,
                        svn_checksum_kind_t kind,
                        apr_pool_t *result_pool);

/* Under the repository db PATH, create a FSFS repository with FORMAT,
 * the given SHARD_SIZE.  If not supported by the respective format,
 * the latter two parameters will be ignored.  FS will be updated.
 *
 * The only file not being written is the 'format' file.  This allows
 * callers such as hotcopy to modify the contents before turning the
 * tree into an accessible repository.
 *
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__create_file_tree(svn_fs_t *fs,
                           const char *path,
                           int format,
                           int shard_size,
                           apr_pool_t *scratch_pool);

/* Create a fs_x fileysystem referenced by FS at path PATH.  Get any
   temporary allocations from SCRATCH_POOL.

   ### Some parts of *FS must have been initialized beforehand; some parts
       (including FS->path) are initialized by this function. */
svn_error_t *
svn_fs_x__create(svn_fs_t *fs,
                 const char *path,
                 apr_pool_t *scratch_pool);

/* Set the uuid of repository FS to UUID and the instance ID to INSTANCE_ID.
   If any of them is NULL, use a newly generated UUID / ID instead.

   If OVERWRITE is not set, the uuid file must not exist yet implying this
   is a fresh repository.

   Perform temporary allocations in SCRATCH_POOL. */
svn_error_t *
svn_fs_x__set_uuid(svn_fs_t *fs,
                   const char *uuid,
                   const char *instance_id,
                   svn_boolean_t overwrite,
                   apr_pool_t *scratch_pool);

/* Read the format number and maximum number of files per directory
   from PATH and return them in *PFORMAT and *MAX_FILES_PER_DIR
   respectively.

   *MAX_FILES_PER_DIR is obtained from the 'layout' format option, and
   will be set to zero if a linear scheme should be used.

   Use SCRATCH_POOL for temporary allocation. */
svn_error_t *
svn_fs_x__write_format(svn_fs_t *fs,
                       svn_boolean_t overwrite,
                       apr_pool_t *scratch_pool);

/* Find the value of the property named PROPNAME in transaction REV.
   Return the contents in *VALUE_P, allocated from RESULT_POOL.
   If REFRESH is not set, continue using the potentially outdated
   revprop generation value in FS->FSAP_DATA.
   Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_fs_x__revision_prop(svn_string_t **value_p,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        const char *propname,
                        svn_boolean_t refresh,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Change, add, or delete a property on a revision REV in filesystem
   FS.  NAME gives the name of the property, and value, if non-NULL,
   gives the new contents of the property.  If value is NULL, then the
   property will be deleted.  If OLD_VALUE_P is not NULL, do nothing unless
   the preexisting value is *OLD_VALUE_P.
   Do any temporary allocation in SCRATCH_POOL.  */
svn_error_t *
svn_fs_x__change_rev_prop(svn_fs_t *fs,
                          svn_revnum_t rev,
                          const char *name,
                          const svn_string_t *const *old_value_p,
                          const svn_string_t *value,
                          apr_pool_t *scratch_pool);

/* If directory PATH does not exist, create it and give it the same
   permissions as FS_PATH.  Do any temporary allocation in SCRATCH_POOL. */
svn_error_t *
svn_fs_x__ensure_dir_exists(const char *path,
                            const char *fs_path,
                            apr_pool_t *scratch_pool);

/* Initialize all session-local caches in FS according to the global
   cache settings. Use SCRATCH_POOL for temporary allocations.

   Please note that it is permissible for this function to set some
   or all of these caches to NULL, regardless of any setting. */
svn_error_t *
svn_fs_x__initialize_caches(svn_fs_t *fs,
                            apr_pool_t *scratch_pool);

#endif
