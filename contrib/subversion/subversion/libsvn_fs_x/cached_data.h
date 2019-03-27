/* cached_data.h --- cached (read) access to FSX data
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

#ifndef SVN_LIBSVN_FS_X_CACHED_DATA_H
#define SVN_LIBSVN_FS_X_CACHED_DATA_H

#include "svn_pools.h"
#include "svn_fs.h"

#include "fs.h"
#include "index.h"



/* Set *NODEREV_P to the node-revision for the node ID in FS.  Do any
   allocations in POOL. */
svn_error_t *
svn_fs_x__get_node_revision(svn_fs_x__noderev_t **noderev_p,
                            svn_fs_t *fs,
                            const svn_fs_x__id_t *id,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/* Set *COUNT to the value of the mergeinfo_count member of the node-
   revision for the node ID in FS.  Do temporary allocations in SCRATCH_POOL.
 */
svn_error_t *
svn_fs_x__get_mergeinfo_count(apr_int64_t *count,
                              svn_fs_t *fs,
                              const svn_fs_x__id_t *id,
                              apr_pool_t *scratch_pool);

/* Verify that representation REP in FS can be accessed.
   Do any allocations in SCRATCH_POOL. */
svn_error_t *
svn_fs_x__check_rep(svn_fs_x__representation_t *rep,
                    svn_fs_t *fs,
                    apr_pool_t *scratch_pool);

/* Follow the representation delta chain in FS starting with REP.  The
   number of reps (including REP) in the chain will be returned in
   *CHAIN_LENGTH.  *SHARD_COUNT will be set to the number of shards
   accessed.  Do any allocations in SCRATCH_POOL. */
svn_error_t *
svn_fs_x__rep_chain_length(int *chain_length,
                           int *shard_count,
                           svn_fs_x__representation_t *rep,
                           svn_fs_t *fs,
                           apr_pool_t *scratch_pool);

/* Set *CONTENTS_P to be a readable svn_stream_t that receives the text
   representation REP as seen in filesystem FS.  If CACHE_FULLTEXT is
   not set, bypass fulltext cache lookup for this rep and don't put the
   reconstructed fulltext into cache.
   Allocate *CONTENT_P in RESULT_POOL. */
svn_error_t *
svn_fs_x__get_contents(svn_stream_t **contents_p,
                       svn_fs_t *fs,
                       svn_fs_x__representation_t *rep,
                       svn_boolean_t cache_fulltext,
                       apr_pool_t *result_pool);

/* Set *CONTENTS_P to be a readable svn_stream_t that receives the text
   representation REP as seen in filesystem FS.  Read the latest element
   of the delta chain from FILE at offset OFFSET.
   Use POOL for allocations. */
svn_error_t *
svn_fs_x__get_contents_from_file(svn_stream_t **contents_p,
                                 svn_fs_t *fs,
                                 svn_fs_x__representation_t *rep,
                                 apr_file_t *file,
                                 apr_off_t offset,
                                 apr_pool_t *pool);

/* Determine on-disk and expanded sizes of the representation identified
 * by ENTRY in FS and return the result in PACKED_LEN and EXPANDED_LEN,
 * respectively.  FILE must point to the start of the representation and
 * STREAM must be a stream defined on top of FILE.
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__get_representation_length(svn_filesize_t *packed_len,
                                    svn_filesize_t *expanded_len,
                                    svn_fs_t *fs,
                                    svn_fs_x__revision_file_t *rev_file,
                                    svn_fs_x__p2l_entry_t* entry,
                                    apr_pool_t *scratch_pool);

/* Attempt to fetch the text representation of node-revision NODEREV as
   seen in filesystem FS and pass it along with the BATON to the PROCESSOR.
   Set *SUCCESS only of the data could be provided and the processing
   had been called.
   Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__try_process_file_contents(svn_boolean_t *success,
                                    svn_fs_t *fs,
                                    svn_fs_x__noderev_t *noderev,
                                    svn_fs_process_contents_func_t processor,
                                    void* baton,
                                    apr_pool_t *scratch_pool);

/* Set *STREAM_P to a delta stream turning the contents of the file SOURCE
   into the contents of the file TARGET, allocated in RESULT_POOL.
   If SOURCE is NULL, an empty string will be used in its stead.
   Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__get_file_delta_stream(svn_txdelta_stream_t **stream_p,
                                svn_fs_t *fs,
                                svn_fs_x__noderev_t *source,
                                svn_fs_x__noderev_t *target,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool);

/* Set *ENTRIES to an apr_array_header_t of dirent structs that contain
   the directory entries of node-revision NODEREV in filesystem FS.  The
   returned table is allocated in RESULT_POOL and entries are sorted
   lexicographically.  SCRATCH_POOL is used for temporary allocations. */
svn_error_t *
svn_fs_x__rep_contents_dir(apr_array_header_t **entries_p,
                           svn_fs_t *fs,
                           svn_fs_x__noderev_t *noderev,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/* Return the directory entry from ENTRIES that matches NAME.  If no such
   entry exists, return NULL.  If HINT is not NULL, set *HINT to the array
   index of the entry returned.  Successive calls in a linear scan scenario
   will be faster called with the same HINT variable. */
svn_fs_x__dirent_t *
svn_fs_x__find_dir_entry(apr_array_header_t *entries,
                         const char *name,
                         int *hint);

/* Set *DIRENT to the entry identified by NAME in the directory given
   by NODEREV in filesystem FS.  If no such entry exits, *DIRENT will
   be NULL.  The value referenced by HINT can be used to speed up
   consecutive calls when travering the directory in name order.
   Any value is allowed, however APR_SIZE_MAX gives best performance
   when there has been no previous lookup for the same directory.

   The returned object is allocated in RESULT_POOL; SCRATCH_POOL
   used for temporary allocations. */
svn_error_t *
svn_fs_x__rep_contents_dir_entry(svn_fs_x__dirent_t **dirent,
                                 svn_fs_t *fs,
                                 svn_fs_x__noderev_t *noderev,
                                 const char *name,
                                 apr_size_t *hint,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

/* Set *PROPLIST to be an apr_hash_t containing the property list of
   node-revision NODEREV as seen in filesystem FS.  Allocate the result
   in RESULT_POOL and use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_fs_x__get_proplist(apr_hash_t **proplist,
                       svn_fs_t *fs,
                       svn_fs_x__noderev_t *noderev,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);

/* Create a changes retrieval context object in *RESULT_POOL and return it
 * in *CONTEXT.  It will allow svn_fs_x__get_changes to fetch consecutive
 * blocks (one per invocation) from REV's changed paths list in FS.
 * Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_fs_x__create_changes_context(svn_fs_x__changes_context_t **context,
                                 svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

/* Fetch the block of changes from the CONTEXT and return it in *CHANGES.
 * Allocate the result in RESULT_POOL and use SCRATCH_POOL for temporaries.
 */
svn_error_t *
svn_fs_x__get_changes(apr_array_header_t **changes,
                      svn_fs_x__changes_context_t *context,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool);

#endif
