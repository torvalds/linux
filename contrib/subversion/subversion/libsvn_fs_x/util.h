/* util.h --- utility functions for FSX repo access
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

#ifndef SVN_LIBSVN_FS_X_UTIL_H
#define SVN_LIBSVN_FS_X_UTIL_H

#include "svn_fs.h"
#include "id.h"
#include "batch_fsync.h"

/* Functions for dealing with recoverable errors on mutable files
 *
 * Revprops, current, and txn-current files are mutable; that is, they
 * change as part of normal fsx operation, in constrat to revs files, or
 * the format file, which are written once at create (or upgrade) time.
 * When more than one host writes to the same repository, we will
 * sometimes see these recoverable errors when accesssing these files.
 *
 * These errors all relate to NFS, and thus we only use this retry code if
 * ESTALE is defined.
 *
 ** ESTALE
 *
 * In NFS v3 and under, the server doesn't track opened files.  If you
 * unlink(2) or rename(2) a file held open by another process *on the
 * same host*, that host's kernel typically renames the file to
 * .nfsXXXX and automatically deletes that when it's no longer open,
 * but this behavior is not required.
 *
 * For obvious reasons, this does not work *across hosts*.  No one
 * knows about the opened file; not the server, and not the deleting
 * client.  So the file vanishes, and the reader gets stale NFS file
 * handle.
 *
 ** EIO, ENOENT
 *
 * Some client implementations (at least the 2.6.18.5 kernel that ships
 * with Ubuntu Dapper) sometimes give spurious ENOENT (only on open) or
 * even EIO errors when trying to read these files that have been renamed
 * over on some other host.
 *
 ** Solution
 *
 * Try open and read of such files in try_stringbuf_from_file().  Call
 * this function within a loop of SVN_FS_X__RECOVERABLE_RETRY_COUNT
 * iterations (though, realistically, the second try will succeed).
 */

#define SVN_FS_X__RECOVERABLE_RETRY_COUNT 10

/* Pathname helper functions */

/* Return TRUE is REV is packed in FS, FALSE otherwise. */
svn_boolean_t
svn_fs_x__is_packed_rev(svn_fs_t *fs,
                        svn_revnum_t rev);

/* Return TRUE is REV is packed in FS, FALSE otherwise. */
svn_boolean_t
svn_fs_x__is_packed_revprop(svn_fs_t *fs,
                            svn_revnum_t rev);

/* Return the first revision in the pack / rev file containing REV in
 * filesystem FS.  For non-packed revs, this will simply be REV. */
svn_revnum_t
svn_fs_x__packed_base_rev(svn_fs_t *fs,
                          svn_revnum_t rev);

/* Return the number of revisions in the pack / rev file in FS that contains
 * revision REV. */
svn_revnum_t
svn_fs_x__pack_size(svn_fs_t *fs, svn_revnum_t rev);

/* Return the full path of the "format" file in FS.
 * The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_format(svn_fs_t *fs,
                      apr_pool_t *result_pool);

/* Return the path to the 'current' file in FS.
   Perform allocation in RESULT_POOL. */
const char *
svn_fs_x__path_current(svn_fs_t *fs,
                       apr_pool_t *result_pool);

/* Return the path to the 'next' file in FS.
   Perform allocation in RESULT_POOL. */
const char *
svn_fs_x__path_next(svn_fs_t *fs,
                    apr_pool_t *result_pool);

/* Return the full path of the "uuid" file in FS.
 * The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_uuid(svn_fs_t *fs,
                    apr_pool_t *result_pool);

/* Return the full path of the "txn-current" file in FS.
 * The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_txn_current(svn_fs_t *fs,
                           apr_pool_t *result_pool);

/* Return the full path of the "txn-current-lock" file in FS.
 * The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_txn_current_lock(svn_fs_t *fs,
                                apr_pool_t *result_pool);

/* Return the full path of the global write lock file in FS.
 * The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_lock(svn_fs_t *fs,
                    apr_pool_t *result_pool);

/* Return the full path of the pack operation lock file in FS.
 * The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_pack_lock(svn_fs_t *fs,
                         apr_pool_t *result_pool);

/* Return the full path of the revprop generation file in FS.
 * Allocate the result in RESULT_POOL.
 */
const char *
svn_fs_x__path_revprop_generation(svn_fs_t *fs,
                                  apr_pool_t *result_pool);

/* Return the path of the pack-related file that for revision REV in FS.
 * KIND specifies the file name base, e.g. "pack".
 * The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_rev_packed(svn_fs_t *fs,
                          svn_revnum_t rev,
                          const char *kind,
                          apr_pool_t *result_pool);

/* Return the full path of the rev shard directory that will contain
 * revision REV in FS.  Allocate the result in RESULT_POOL.
 */
const char *
svn_fs_x__path_shard(svn_fs_t *fs,
                     svn_revnum_t rev,
                     apr_pool_t *result_pool);

/* Return the full path of the non-packed rev file containing revision REV
 * in FS.  Allocate the result in RESULT_POOL.
 */
const char *
svn_fs_x__path_rev(svn_fs_t *fs,
                   svn_revnum_t rev,
                   apr_pool_t *result_pool);

/* Set *PATH to the path of REV in FS, whether in a pack file or not.
   Allocate *PATH in RESULT_POOL.

   Note: If the caller does not have the write lock on FS, then the path is
   not guaranteed to be correct or to remain correct after the function
   returns, because the revision might become packed before or after this
   call.  If a file exists at that path, then it is correct; if not, then
   the caller should call update_min_unpacked_rev() and re-try once. */
const char *
svn_fs_x__path_rev_absolute(svn_fs_t *fs,
                            svn_revnum_t rev,
                            apr_pool_t *result_pool);

/* Return the full path of the pack shard directory that will contain the
 * packed revision REV in FS.  Allocate the result in RESULT_POOL.
 */
const char *
svn_fs_x__path_pack_shard(svn_fs_t *fs,
                          svn_revnum_t rev,
                          apr_pool_t *result_pool);

/* Return the full path of the non-packed revision properties file that
 * contains the props for revision REV in FS.
 * Allocate the result in RESULT_POOL.
 */
const char *
svn_fs_x__path_revprops(svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_pool_t *result_pool);

/* Convert the TXN_ID into a string, allocated from RESULT_POOL.
 */
const char *
svn_fs_x__txn_name(svn_fs_x__txn_id_t txn_id,
                   apr_pool_t *result_pool);

/* Convert TXN_NAME into an ID and return it in *TXN_ID. */
svn_error_t *
svn_fs_x__txn_by_name(svn_fs_x__txn_id_t *txn_id,
                      const char *txn_name);

/* Return the path of the directory containing the transaction TXN_ID in FS.
 * The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_txn_dir(svn_fs_t *fs,
                       svn_fs_x__txn_id_t txn_id,
                       apr_pool_t *result_pool);

/* Return the path of the 'transactions' directory in FS.
 * The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_txns_dir(svn_fs_t *fs,
                        apr_pool_t *result_pool);

/* Return the name of the sha1->rep mapping file in transaction TXN_ID
 * within FS for the given SHA1 checksum.  Use POOL for allocations.
 */
const char *
svn_fs_x__path_txn_sha1(svn_fs_t *fs,
                        svn_fs_x__txn_id_t txn_id,
                        const unsigned char *sha1,
                        apr_pool_t *pool);

/* Return the path of the 'txn-protorevs' directory in FS, even if that
 * folder may not exist in FS.  The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_txn_proto_revs(svn_fs_t *fs,
                              apr_pool_t *result_pool);

/* Return the path of the changes file for transaction TXN_ID in FS.
 * The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_txn_changes(svn_fs_t *fs,
                           svn_fs_x__txn_id_t txn_id,
                           apr_pool_t *result_pool);

/* Return the path of the file containing the log-to-phys index for
 * the transaction identified by TXN_ID in FS.
 * The result will be allocated in RESULT_POOL.
 */
const char*
svn_fs_x__path_l2p_proto_index(svn_fs_t *fs,
                               svn_fs_x__txn_id_t txn_id,
                               apr_pool_t *result_pool);

/* Return the path of the file containing the phys-to-log index for
 * the transaction identified by TXN_ID in FS.
 * The result will be allocated in RESULT_POOL.
 */
const char*
svn_fs_x__path_p2l_proto_index(svn_fs_t *fs,
                               svn_fs_x__txn_id_t txn_id,
                               apr_pool_t *result_pool);

/* Return the path of the file containing the transaction properties for
 * the transaction identified by TXN_ID in FS.
 * The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_txn_props(svn_fs_t *fs,
                         svn_fs_x__txn_id_t txn_id,
                         apr_pool_t *result_pool);

/* Return the path of the file containing the node and copy ID counters for
 * the transaction identified by TXN_ID in FS.
 * The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_txn_next_ids(svn_fs_t *fs,
                            svn_fs_x__txn_id_t txn_id,
                            apr_pool_t *result_pool);

/* Return the path of the file storing the oldest non-packed revision in FS.
 * The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_min_unpacked_rev(svn_fs_t *fs,
                                apr_pool_t *result_pool);

/* Return the path of the file containing item_index counter for
 * the transaction identified by TXN_ID in FS.
 * The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_txn_item_index(svn_fs_t *fs,
                              svn_fs_x__txn_id_t txn_id,
                              apr_pool_t *result_pool);

/* Return the path of the proto-revision file for transaction TXN_ID in FS.
 * The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_txn_proto_rev(svn_fs_t *fs,
                             svn_fs_x__txn_id_t txn_id,
                             apr_pool_t *result_pool);

/* Return the path of the proto-revision lock file for transaction TXN_ID
 * in FS.  The result will be allocated in RESULT_POOL.
 */
const char *
svn_fs_x__path_txn_proto_rev_lock(svn_fs_t *fs,
                                  svn_fs_x__txn_id_t txn_id,
                                  apr_pool_t *result_pool);

/* Return the path of the file containing the in-transaction node revision
 * identified by ID in FS.
 * The result will be allocated in RESULT_POOL, temporaries in SCRATCH_POOL.
 */
const char *
svn_fs_x__path_txn_node_rev(svn_fs_t *fs,
                            const svn_fs_x__id_t *id,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/* Return the path of the file containing the in-transaction properties of
 * the node identified by ID in FS.
 * The result will be allocated in RESULT_POOL, temporaries in SCRATCH_POOL.
 */
const char *
svn_fs_x__path_txn_node_props(svn_fs_t *fs,
                              const svn_fs_x__id_t *id,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);

/* Return the path of the file containing the directory entries of the
 * in-transaction directory node identified by ID in FS.
 * The result will be allocated in RESULT_POOL, temporaries in SCRATCH_POOL.
 */
const char *
svn_fs_x__path_txn_node_children(svn_fs_t *fs,
                                 const svn_fs_x__id_t *id,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

/* Check that BUF, a nul-terminated buffer of text from file PATH,
   contains only digits at OFFSET and beyond, raising an error if not.
   TITLE contains a user-visible description of the file, usually the
   short file name.

   Uses SCRATCH_POOL for temporary allocation. */
svn_error_t *
svn_fs_x__check_file_buffer_numeric(const char *buf,
                                    apr_off_t offset,
                                    const char *path,
                                    const char *title,
                                    apr_pool_t *scratch_pool);

/* Set *MIN_UNPACKED_REV to the integer value read from the file returned
 * by #svn_fs_x__path_min_unpacked_rev() for FS.
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__read_min_unpacked_rev(svn_revnum_t *min_unpacked_rev,
                                svn_fs_t *fs,
                                apr_pool_t *scratch_pool);

/* Re-read the MIN_UNPACKED_REV member of FS from disk.
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__update_min_unpacked_rev(svn_fs_t *fs,
                                  apr_pool_t *scratch_pool);

/* Atomically update the 'min-unpacked-rev' file in FS to hold the specifed
 * REVNUM.  Perform temporary allocations in SCRATCH_POOL.
 */
svn_error_t *
svn_fs_x__write_min_unpacked_rev(svn_fs_t *fs,
                                 svn_revnum_t revnum,
                                 apr_pool_t *scratch_pool);

/* Set *REV to the value read from the 'current' file.  Perform temporary
 * allocations in SCRATCH_POOL.
 */
svn_error_t *
svn_fs_x__read_current(svn_revnum_t *rev,
                       svn_fs_t *fs,
                       apr_pool_t *scratch_pool);

/* Atomically update the 'current' file to hold the specifed REV.
   Perform temporary allocations in SCRATCH_POOL. */
svn_error_t *
svn_fs_x__write_current(svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_pool_t *scratch_pool);

/* Read the file at PATH and return its content in *CONTENT, allocated in
 * RESULT_POOL.  *CONTENT will not be modified unless the whole file was
 * read successfully.
 *
 * ESTALE, EIO and ENOENT will not cause this function to return an error
 * unless LAST_ATTEMPT has been set.  If MISSING is not NULL, indicate
 * missing files (ENOENT) there.
 */
svn_error_t *
svn_fs_x__try_stringbuf_from_file(svn_stringbuf_t **content,
                                  svn_boolean_t *missing,
                                  const char *path,
                                  svn_boolean_t last_attempt,
                                  apr_pool_t *result_pool);

/* Read the file FNAME and store the contents in *BUF.
   Allocations are performed in RESULT_POOL. */
svn_error_t *
svn_fs_x__read_content(svn_stringbuf_t **content,
                       const char *fname,
                       apr_pool_t *result_pool);

/* Reads a line from STREAM and converts it to a 64 bit integer to be
 * returned in *RESULT.  If we encounter eof, set *HIT_EOF and leave
 * *RESULT unchanged.  If HIT_EOF is NULL, EOF causes an "corrupt FS"
 * error return.
 * SCRATCH_POOL is used for temporary allocations.
 */
svn_error_t *
svn_fs_x__read_number_from_stream(apr_int64_t *result,
                                  svn_boolean_t *hit_eof,
                                  svn_stream_t *stream,
                                  apr_pool_t *scratch_pool);

/* Move a file into place from temporary OLD_FILENAME to its final
   location NEW_FILENAME, which must be on to the same volume.  Schedule
   any necessary fsync calls in BATCH.  On Unix, match the permissions
   of the new file to the permissions of PERMS_REFERENCE.

   Temporary allocations are from SCRATCH_POOL.

   This function almost duplicates svn_io_file_move(), but it tries to
   guarantee a flush if BATCH->FLUSH_TO_DISK is non-zero. */
svn_error_t *
svn_fs_x__move_into_place(const char *old_filename,
                          const char *new_filename,
                          const char *perms_reference,
                          svn_fs_x__batch_fsync_t *batch,
                          apr_pool_t *scratch_pool);

#endif
