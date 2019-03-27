/* cached_data.h --- cached (read) access to FSFS data
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

#ifndef SVN_LIBSVN_FS__CACHED_DATA_H
#define SVN_LIBSVN_FS__CACHED_DATA_H

#include "svn_pools.h"
#include "svn_fs.h"

#include "fs.h"



/* Resolve a FSFS quirk: if REP in FS is a "PLAIN" representation, its
 * EXPANDED_SIZE element may be 0, in which case its value has to be taken
 * from SIZE.
 *
 * This function ensures that EXPANDED_SIZE in REP always contains the
 * actual value. No-op if REP is NULL.  Uses SCRATCH_POOL for temporaries.
 */
svn_error_t *
svn_fs_fs__fixup_expanded_size(svn_fs_t *fs,
                               representation_t *rep,
                               apr_pool_t *scratch_pool);

/* Set *NODEREV_P to the node-revision for the node ID in FS.  Do any
   allocations in POOL. */
svn_error_t *
svn_fs_fs__get_node_revision(node_revision_t **noderev_p,
                             svn_fs_t *fs,
                             const svn_fs_id_t *id,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

/* Set *ROOT_ID to the node-id for the root of revision REV in
   filesystem FS.  Do any allocations in POOL. */
svn_error_t *
svn_fs_fs__rev_get_root(svn_fs_id_t **root_id,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Verify that representation REP in FS can be accessed.  Successive calls
   to this function should pass a non-NULL value to HINT.  In that case,
   many file open / close operations can be eliminated.
   Do any allocations in SCRATCH_POOL. */
svn_error_t *
svn_fs_fs__check_rep(representation_t *rep,
                     svn_fs_t *fs,
                     void **hint,
                     apr_pool_t *scratch_pool);

/* Follow the representation delta chain in FS starting with REP.  The
   number of reps (including REP) in the chain will be returned in
   *CHAIN_LENGTH.  *SHARD_COUNT will be set to the number of shards
   accessed.  Do any allocations in SCRATCH_POOL. */
svn_error_t *
svn_fs_fs__rep_chain_length(int *chain_length,
                            int *shard_count,
                            representation_t *rep,
                            svn_fs_t *fs,
                            apr_pool_t *scratch_pool);

/* Set *CONTENTS_P to be a readable svn_stream_t that receives the text
   representation REP as seen in filesystem FS.  If CACHE_FULLTEXT is
   not set, bypass fulltext cache lookup for this rep and don't put the
   reconstructed fulltext into cache.
   Use POOL for allocations. */
svn_error_t *
svn_fs_fs__get_contents(svn_stream_t **contents_p,
                        svn_fs_t *fs,
                        representation_t *rep,
                        svn_boolean_t cache_fulltext,
                        apr_pool_t *pool);

/* Set *CONTENTS_P to be a readable svn_stream_t that receives the text
   representation REP as seen in filesystem FS.  Read the latest element
   of the delta chain from FILE at offset OFFSET.
   Use POOL for allocations. */
svn_error_t *
svn_fs_fs__get_contents_from_file(svn_stream_t **contents_p,
                                  svn_fs_t *fs,
                                  representation_t *rep,
                                  apr_file_t *file,
                                  apr_off_t offset,
                                  apr_pool_t *pool);

/* Attempt to fetch the text representation of node-revision NODEREV as
   seen in filesystem FS and pass it along with the BATON to the PROCESSOR.
   Set *SUCCESS only of the data could be provided and the processing
   had been called.
   Use POOL for all allocations.
 */
svn_error_t *
svn_fs_fs__try_process_file_contents(svn_boolean_t *success,
                                     svn_fs_t *fs,
                                     node_revision_t *noderev,
                                     svn_fs_process_contents_func_t processor,
                                     void* baton,
                                     apr_pool_t *pool);

/* Set *STREAM_P to a delta stream turning the contents of the file SOURCE into
   the contents of the file TARGET, allocated in POOL.
   If SOURCE is null, the empty string will be used. */
svn_error_t *
svn_fs_fs__get_file_delta_stream(svn_txdelta_stream_t **stream_p,
                                 svn_fs_t *fs,
                                 node_revision_t *source,
                                 node_revision_t *target,
                                 apr_pool_t *pool);

/* Set *ENTRIES to an apr_array_header_t of dirent structs that contain
   the directory entries of node-revision NODEREV in filesystem FS.  The
   returned table is allocated in RESULT_POOL and entries are sorted
   lexicographically.  SCRATCH_POOL is used for temporary allocations. */
svn_error_t *
svn_fs_fs__rep_contents_dir(apr_array_header_t **entries_p,
                            svn_fs_t *fs,
                            node_revision_t *noderev,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/* Return the directory entry from ENTRIES that matches NAME.  If no such
   entry exists, return NULL.  If HINT is not NULL, set *HINT to the array
   index of the entry returned.  Successive calls in a linear scan scenario
   will be faster called with the same HINT variable. */
svn_fs_dirent_t *
svn_fs_fs__find_dir_entry(apr_array_header_t *entries,
                          const char *name,
                          int *hint);

/* Set *DIRENT to the entry identified by NAME in the directory given
   by NODEREV in filesystem FS.  If no such entry exits, *DIRENT will
   be NULL. The returned object is allocated in RESULT_POOL; SCRATCH_POOL
   used for temporary allocations. */
svn_error_t *
svn_fs_fs__rep_contents_dir_entry(svn_fs_dirent_t **dirent,
                                  svn_fs_t *fs,
                                  node_revision_t *noderev,
                                  const char *name,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool);

/* Set *PROPLIST to be an apr_hash_t containing the property list of
   node-revision NODEREV as seen in filesystem FS.  Use POOL for
   temporary allocations. */
svn_error_t *
svn_fs_fs__get_proplist(apr_hash_t **proplist,
                        svn_fs_t *fs,
                        node_revision_t *noderev,
                        apr_pool_t *pool);

/* Create a changes retrieval context object in *RESULT_POOL and return it
 * in *CONTEXT.  It will allow svn_fs_x__get_changes to fetch consecutive
 * blocks (one per invocation) from REV's changed paths list in FS. */
svn_error_t *
svn_fs_fs__create_changes_context(svn_fs_fs__changes_context_t **context,
                                  svn_fs_t *fs,
                                  svn_revnum_t rev,
                                  apr_pool_t *result_pool);

/* Fetch the block of changes from the CONTEXT and return it in *CHANGES.
 * Allocate the result in RESULT_POOL and use SCRATCH_POOL for temporaries.
 */
svn_error_t *
svn_fs_fs__get_changes(apr_array_header_t **changes,
                       svn_fs_fs__changes_context_t *context,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);

#endif
