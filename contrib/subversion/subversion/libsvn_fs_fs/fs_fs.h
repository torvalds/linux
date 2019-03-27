/* fs_fs.h : interface to the native filesystem layer
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

#ifndef SVN_LIBSVN_FS__FS_FS_H
#define SVN_LIBSVN_FS__FS_FS_H

#include "fs.h"

/* Read the 'format' file of fsfs filesystem FS and store its info in FS.
 * Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_fs_fs__read_format_file(svn_fs_t *fs, apr_pool_t *scratch_pool);

/* Open the fsfs filesystem pointed to by PATH and associate it with
   filesystem object FS.  Use POOL for temporary allocations.

   ### Some parts of *FS must have been initialized beforehand; some parts
       (including FS->path) are initialized by this function. */
svn_error_t *svn_fs_fs__open(svn_fs_t *fs,
                             const char *path,
                             apr_pool_t *pool);

/* Initialize parts of the FS data that are being shared across multiple
   filesystem objects.  Use COMMON_POOL for process-wide and POOL for
   temporary allocations.  Use COMMON_POOL_LOCK to ensure that the
   initialization is serialized. */
svn_error_t *svn_fs_fs__initialize_shared_data(svn_fs_t *fs,
                                               svn_mutex__t *common_pool_lock,
                                               apr_pool_t *pool,
                                               apr_pool_t *common_pool);

/* Upgrade the fsfs filesystem FS.  Indicate progress via the optional
 * NOTIFY_FUNC callback using NOTIFY_BATON.  The optional CANCEL_FUNC
 * will periodically be called with CANCEL_BATON to allow for preemption.
 * Use POOL for temporary allocations. */
svn_error_t *svn_fs_fs__upgrade(svn_fs_t *fs,
                                svn_fs_upgrade_notify_t notify_func,
                                void *notify_baton,
                                svn_cancel_func_t cancel_func,
                                void *cancel_baton,
                                apr_pool_t *pool);

/* Set *YOUNGEST to the youngest revision in filesystem FS.  Do any
   temporary allocation in POOL. */
svn_error_t *svn_fs_fs__youngest_rev(svn_revnum_t *youngest,
                                     svn_fs_t *fs,
                                     apr_pool_t *pool);

/* Return the shard size of filesystem FS.  Return 0 for non-shared ones. */
int
svn_fs_fs__shard_size(svn_fs_t *fs);

/* Set *MIN_UNPACKED to the oldest non-packed revision in filesystem FS.
   Do any temporary allocation in POOL. */
svn_error_t *
svn_fs_fs__min_unpacked_rev(svn_revnum_t *min_unpacked,
                            svn_fs_t *fs,
                            apr_pool_t *pool);

/* Return SVN_ERR_FS_NO_SUCH_REVISION if the given revision REV is newer
   than the current youngest revision in FS or is simply not a valid
   revision number, else return success. */
svn_error_t *
svn_fs_fs__ensure_revision_exists(svn_revnum_t rev,
                                  svn_fs_t *fs,
                                  apr_pool_t *pool);

/* Set *LENGTH to the be fulltext length of the node revision
   specified by NODEREV.  Use POOL for temporary allocations. */
svn_error_t *svn_fs_fs__file_length(svn_filesize_t *length,
                                    node_revision_t *noderev,
                                    apr_pool_t *pool);

/* Return TRUE if the representation keys in A and B both point to the
   same representation, else return FALSE. */
svn_boolean_t svn_fs_fs__noderev_same_rep_key(representation_t *a,
                                              representation_t *b);

/* Set *EQUAL to TRUE if the text representations in A and B within FS
   have equal contents, else set it to FALSE.
   Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_fs_fs__file_text_rep_equal(svn_boolean_t *equal,
                               svn_fs_t *fs,
                               node_revision_t *a,
                               node_revision_t *b,
                               apr_pool_t *scratch_pool);

/* Set *EQUAL to TRUE if the property representations in A and B within FS
   have equal contents, else set it to FALSE.
   Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_fs_fs__prop_rep_equal(svn_boolean_t *equal,
                          svn_fs_t *fs,
                          node_revision_t *a,
                          node_revision_t *b,
                          apr_pool_t *scratch_pool);


/* Return a copy of the representation REP allocated from POOL. */
representation_t *svn_fs_fs__rep_copy(representation_t *rep,
                                      apr_pool_t *pool);


/* Return the recorded checksum of type KIND for the text representation
   of NODREV into CHECKSUM, allocating from POOL.  If no stored checksum is
   available, put all NULL into CHECKSUM. */
svn_error_t *svn_fs_fs__file_checksum(svn_checksum_t **checksum,
                                      node_revision_t *noderev,
                                      svn_checksum_kind_t kind,
                                      apr_pool_t *pool);

/* Return whether or not the given FS supports mergeinfo metadata. */
svn_boolean_t svn_fs_fs__fs_supports_mergeinfo(svn_fs_t *fs);

/* Under the repository db PATH, create a FSFS repository with FORMAT,
 * the given SHARD_SIZE. If USE_LOG_ADDRESSING is non-zero, repository
 * will use logical addressing. If not supported by the respective format,
 * the latter two parameters will be ignored. FS will be updated.
 *
 * The only file not being written is the 'format' file.  This allows
 * callers such as hotcopy to modify the contents before turning the
 * tree into an accessible repository.
 *
 * Use POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__create_file_tree(svn_fs_t *fs,
                            const char *path,
                            int format,
                            int shard_size,
                            svn_boolean_t use_log_addressing,
                            apr_pool_t *pool);

/* Create a fs_fs fileysystem referenced by FS at path PATH.  Get any
   temporary allocations from POOL.

   ### Some parts of *FS must have been initialized beforehand; some parts
       (including FS->path) are initialized by this function. */
svn_error_t *svn_fs_fs__create(svn_fs_t *fs,
                               const char *path,
                               apr_pool_t *pool);

/* Set the uuid of repository FS to UUID and the instance ID to INSTANCE_ID.
   If any of them is NULL, use a newly generated UUID / ID instead.  Ignore
   INSTANCE_ID whenever instance IDs are not supported by the FS format.
   Perform temporary allocations in POOL. */
svn_error_t *svn_fs_fs__set_uuid(svn_fs_t *fs,
                                 const char *uuid,
                                 const char *instance_id,
                                 apr_pool_t *pool);

/* Return the path to the 'current' file in FS.
   Perform allocation in POOL. */
const char *
svn_fs_fs__path_current(svn_fs_t *fs, apr_pool_t *pool);

/* Write the format number and maximum number of files per directory
   for FS, possibly expecting to overwrite a previously existing file.

   Use POOL for temporary allocation. */
svn_error_t *
svn_fs_fs__write_format(svn_fs_t *fs,
                        svn_boolean_t overwrite,
                        apr_pool_t *pool);

/* Obtain a write lock on the filesystem FS in a subpool of POOL, call
   BODY with BATON and that subpool, destroy the subpool (releasing the write
   lock) and return what BODY returned. */
svn_error_t *
svn_fs_fs__with_write_lock(svn_fs_t *fs,
                           svn_error_t *(*body)(void *baton,
                                                apr_pool_t *pool),
                           void *baton,
                           apr_pool_t *pool);

/* Obtain a pack operation lock on the filesystem FS in a subpool of POOL,
   call BODY with BATON and that subpool, destroy the subpool (releasing the
   write lock) and return what BODY returned. */
svn_error_t *
svn_fs_fs__with_pack_lock(svn_fs_t *fs,
                          svn_error_t *(*body)(void *baton,
                                               apr_pool_t *pool),
                          void *baton,
                          apr_pool_t *pool);

/* Run BODY (with BATON and POOL) while the txn-current file
   of FS is locked. */
svn_error_t *
svn_fs_fs__with_txn_current_lock(svn_fs_t *fs,
                                 svn_error_t *(*body)(void *baton,
                                                      apr_pool_t *pool),
                                 void *baton,
                                 apr_pool_t *pool);

/* Obtain all locks on the filesystem FS in a subpool of POOL, call BODY
   with BATON and that subpool, destroy the subpool (releasing the locks)
   and return what BODY returned.

   This combines svn_fs_fs__with_write_lock, svn_fs_fs__with_pack_lock,
   and svn_fs_fs__with_txn_current_lock, ensuring correct lock ordering. */
svn_error_t *
svn_fs_fs__with_all_locks(svn_fs_t *fs,
                          svn_error_t *(*body)(void *baton,
                                               apr_pool_t *pool),
                          void *baton,
                          apr_pool_t *pool);

/* Find the value of the property named PROPNAME in revision REV.
   Return the contents in *VALUE_P.  The contents will be allocated
   from RESULT_POOL and SCRATCH_POOL is used for temporaries.
   Invalidate any revprop cache is REFRESH is set. */
svn_error_t *svn_fs_fs__revision_prop(svn_string_t **value_p, svn_fs_t *fs,
                                      svn_revnum_t rev,
                                      const char *propname,
                                      svn_boolean_t refresh,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool);

/* Change, add, or delete a property on a revision REV in filesystem
   FS.  NAME gives the name of the property, and value, if non-NULL,
   gives the new contents of the property.  If value is NULL, then the
   property will be deleted.  If OLD_VALUE_P is not NULL, do nothing unless the
   preexisting value is *OLD_VALUE_P.  Do any temporary allocation in POOL.  */
svn_error_t *svn_fs_fs__change_rev_prop(svn_fs_t *fs, svn_revnum_t rev,
                                        const char *name,
                                        const svn_string_t *const *old_value_p,
                                        const svn_string_t *value,
                                        apr_pool_t *pool);

/* If directory PATH does not exist, create it and give it the same
   permissions as FS_PATH.*/
svn_error_t *svn_fs_fs__ensure_dir_exists(const char *path,
                                          const char *fs_path,
                                          apr_pool_t *pool);

/* Update the node origin index for FS, recording the mapping from
   NODE_ID to NODE_REV_ID.  Use POOL for any temporary allocations.

   Because this is just an "optional" cache, this function does not
   return an error if the underlying storage is readonly; it still
   returns an error for other error conditions.
 */
svn_error_t *
svn_fs_fs__set_node_origin(svn_fs_t *fs,
                           const svn_fs_fs__id_part_t *node_id,
                           const svn_fs_id_t *node_rev_id,
                           apr_pool_t *pool);

/* Set *ORIGIN_ID to the node revision ID from which the history of
   all nodes in FS whose "Node ID" is NODE_ID springs, as determined
   by a look in the index.  ORIGIN_ID needs to be parsed in an
   FS-backend-specific way.  Use POOL for allocations.

   If there is no entry for NODE_ID in the cache, return NULL
   in *ORIGIN_ID. */
svn_error_t *
svn_fs_fs__get_node_origin(const svn_fs_id_t **origin_id,
                           svn_fs_t *fs,
                           const svn_fs_fs__id_part_t *node_id,
                           apr_pool_t *pool);


/* Initialize all session-local caches in FS according to the global
   cache settings. Use POOL for temporary allocations.

   Please note that it is permissible for this function to set some
   or all of these caches to NULL, regardless of any setting. */
svn_error_t *
svn_fs_fs__initialize_caches(svn_fs_t *fs, apr_pool_t *pool);

/* Initialize all transaction-local caches in FS according to the global
   cache settings and make TXN_ID part of their key space. Use POOL for
   allocations.

   Please note that it is permissible for this function to set some or all
   of these caches to NULL, regardless of any setting. */
svn_error_t *
svn_fs_fs__initialize_txn_caches(svn_fs_t *fs,
                                 const char *txn_id,
                                 apr_pool_t *pool);

/* Resets the svn_cache__t structures local to the current transaction in FS.
   Calling it more than once per txn or from outside any txn is allowed. */
void
svn_fs_fs__reset_txn_caches(svn_fs_t *fs);

#endif
