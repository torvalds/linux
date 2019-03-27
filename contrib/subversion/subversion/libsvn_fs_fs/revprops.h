/* revprops.h --- everything needed to handle revprops in FSFS
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

#include "svn_fs.h"

/* In the filesystem FS, pack all revprop shards up to min_unpacked_rev.
 *
 * NOTE: Keep the old non-packed shards around until after the format bump.
 * Otherwise, re-running upgrade will drop the packed revprop shard but
 * have no unpacked data anymore.  Call upgrade_cleanup_pack_revprops after
 * the bump.
 *
 * NOTIFY_FUNC and NOTIFY_BATON as well as CANCEL_FUNC and CANCEL_BATON are
 * used in the usual way.  Temporary allocations are done in SCRATCH_POOL.
 */
svn_error_t *
svn_fs_fs__upgrade_pack_revprops(svn_fs_t *fs,
                                 svn_fs_upgrade_notify_t notify_func,
                                 void *notify_baton,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 apr_pool_t *scratch_pool);

/* In the filesystem FS, remove all non-packed revprop shards up to
 * min_unpacked_rev.  Temporary allocations are done in SCRATCH_POOL.
 *
 * NOTIFY_FUNC and NOTIFY_BATON as well as CANCEL_FUNC and CANCEL_BATON are
 * used in the usual way.  Cancellation is supported in the sense that we
 * will cleanly abort the operation.  However, there will be remnant shards
 * that must be removed manually.
 *
 * See upgrade_pack_revprops for more info.
 */
svn_error_t *
svn_fs_fs__upgrade_cleanup_pack_revprops(svn_fs_t *fs,
                                         svn_fs_upgrade_notify_t notify_func,
                                         void *notify_baton,
                                         svn_cancel_func_t cancel_func,
                                         void *cancel_baton,
                                         apr_pool_t *scratch_pool);

/* Invalidate the revprop cache in FS. */
void
svn_fs_fs__reset_revprop_cache(svn_fs_t *fs);

/* Read the revprops for revision REV in FS and return them in *PROPERTIES_P.
 * If REFRESH is set, clear the revprop cache before accessing the data.
 *
 * The result will be allocated in RESULT_POOL; SCRATCH_POOL is used for
 * temporaries.
 */
svn_error_t *
svn_fs_fs__get_revision_proplist(apr_hash_t **proplist_p,
                                 svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 svn_boolean_t refresh,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

/* Set the revision property list of revision REV in filesystem FS to
   PROPLIST.  Use POOL for temporary allocations. */
svn_error_t *
svn_fs_fs__set_revision_proplist(svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 apr_hash_t *proplist,
                                 apr_pool_t *pool);


/* Return TRUE, if for REVISION in FS, we can find the revprop pack file.
 * Use POOL for temporary allocations.
 * Set *MISSING, if the reason is a missing manifest or pack file.
 */
svn_boolean_t
svn_fs_fs__packed_revprop_available(svn_boolean_t *missing,
                                    svn_fs_t *fs,
                                    svn_revnum_t revision,
                                    apr_pool_t *pool);


/****** Packing FSFS shards *********/

/* Copy revprop files for revisions [START_REV, END_REV) from SHARD_PATH
 * to the pack file at PACK_FILE_NAME in PACK_FILE_DIR.
 *
 * The file sizes have already been determined and written to SIZES.
 * Please note that this function will be executed while the filesystem
 * has been locked and that revprops files will therefore not be modified
 * while the pack is in progress.
 *
 * COMPRESSION_LEVEL defines how well the resulting pack file shall be
 * compressed or whether is shall be compressed at all.  TOTAL_SIZE is
 * a hint on which initial buffer size we should use to hold the pack file
 * content.
 *
 * If FLUSH_TO_DISK is non-zero, do not return until the data has actually
 * been written on the disk.  CANCEL_FUNC and CANCEL_BATON are used as usual.
 * Temporary allocations are done in SCRATCH_POOL.
 */
svn_error_t *
svn_fs_fs__copy_revprops(const char *pack_file_dir,
                         const char *pack_filename,
                         const char *shard_path,
                         svn_revnum_t start_rev,
                         svn_revnum_t end_rev,
                         apr_array_header_t *sizes,
                         apr_size_t total_size,
                         int compression_level,
                         svn_boolean_t flush_to_disk,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *scratch_pool);

/* In the filesystem FS, pack all revprop shards up to min_unpacked_rev.
 *
 * NOTE: Keep the old non-packed shards around until after the format bump.
 * Otherwise, re-running upgrade will drop the packed revprop shard but
 * have no unpacked data anymore.  Call upgrade_cleanup_pack_revprops after
 * the bump.
 *
 * If FLUSH_TO_DISK is non-zero, do not return until the data has actually
 * been written on the disk.  CANCEL_FUNC and CANCEL_BATON areused in the
 * usual way.  Temporary allocations are done in SCRATCH_POOL.
 */
svn_error_t *
svn_fs_fs__pack_revprops_shard(const char *pack_file_dir,
                               const char *shard_path,
                               apr_int64_t shard,
                               int max_files_per_dir,
                               apr_int64_t max_pack_size,
                               int compression_level,
                               svn_boolean_t flush_to_disk,
                               svn_cancel_func_t cancel_func,
                               void *cancel_baton,
                               apr_pool_t *scratch_pool);

/* In the filesystem FS, remove all non-packed revprop shards up to
 * min_unpacked_rev.  Temporary allocations are done in SCRATCH_POOL.
 *
 * NOTIFY_FUNC and NOTIFY_BATON as well as CANCEL_FUNC and CANCEL_BATON are
 * used in the usual way.  Cancellation is supported in the sense that we
 * will cleanly abort the operation.  However, there will be remnant shards
 * that must be removed manually.
 *
 * See upgrade_pack_revprops for more info.
 */
svn_error_t *
svn_fs_fs__delete_revprops_shard(const char *shard_path,
                                 apr_int64_t shard,
                                 int max_files_per_dir,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 apr_pool_t *scratch_pool);
