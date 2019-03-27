/* reps.h --- FSX representation container
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

#ifndef SVN_LIBSVN_FS_X_REPS_H
#define SVN_LIBSVN_FS_X_REPS_H

#include "svn_io.h"
#include "fs.h"

/* This container type implements the start-delta (aka pick lists) data
 * structure plus functions to create it and read data from it.  The key
 * point is to identify common sub-strings within a whole set of fulltexts
 * instead of only two as in the classic txdelta code.
 *
 * Because it is relatively expensive to optimize the final in-memory
 * layout, representation containers cannot be updated.  A builder object
 * will do most of the space saving when adding fulltexts but the final
 * data will only be created immediately before serializing everything to
 * disk.  So, builders are write only and representation containers are
 * read-only.
 *
 * Extracting data from a representation container is O(length) but it
 * may require multiple iterations if base representations outside the
 * container were used.  Therefore, you will first create an extractor
 * object (this may happen while holding a cache lock) and the you need
 * to "drive" the extractor outside any cache context.
 */

/* A write-only constructor object for representation containers.
 */
typedef struct svn_fs_x__reps_builder_t svn_fs_x__reps_builder_t;

/* A read-only representation container -
 * an opaque collection of fulltexts, i.e. byte strings.
 */
typedef struct svn_fs_x__reps_t svn_fs_x__reps_t;

/* The fulltext extractor utility object.
 */
typedef struct svn_fs_x__rep_extractor_t svn_fs_x__rep_extractor_t;

/* Baton type to be passed to svn_fs_x__reps_get_func.
 */
typedef struct svn_fs_x__reps_baton_t
{
  /* filesystem the resulting extractor shall operate on */
  svn_fs_t *fs;

  /* element index of the item to extract from the container */
  apr_size_t idx;
} svn_fs_x__reps_baton_t;

/* Create and populate noderev containers. */

/* Create and return a new builder object, allocated in RESULT_POOL.
 */
svn_fs_x__reps_builder_t *
svn_fs_x__reps_builder_create(svn_fs_t *fs,
                              apr_pool_t *result_pool);

/* To BUILDER, add reference to the fulltext currently stored in
 * representation REP.  Substrings matching with any of the base reps
 * in BUILDER can be removed from the text base and be replaced by
 * references to those base representations.
 *
 * The PRIORITY is a mere hint on which base representations should
 * preferred in case we could re-use the same contents from multiple bases.
 * Higher numerical value means higher priority / likelihood of being
 * selected over others.
 *
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__reps_add_base(svn_fs_x__reps_builder_t *builder,
                        svn_fs_x__representation_t *rep,
                        int priority,
                        apr_pool_t *scratch_pool);

/* Add the byte string CONTENTS to BUILDER.  Return the item index under
 * which the fulltext can be retrieved from the final container in *REP_IDX.
 */
svn_error_t *
svn_fs_x__reps_add(apr_size_t *rep_idx,
                   svn_fs_x__reps_builder_t *builder,
                   const svn_string_t *contents);

/* Return a rough estimate in bytes for the serialized representation
 * of BUILDER.
 */
apr_size_t
svn_fs_x__reps_estimate_size(const svn_fs_x__reps_builder_t *builder);

/* Read from representation containers. */

/* For fulltext IDX in CONTAINER in filesystem FS, create an extract object
 * allocated in RESULT_POOL and return it in *EXTRACTOR.
 */
svn_error_t *
svn_fs_x__reps_get(svn_fs_x__rep_extractor_t **extractor,
                   svn_fs_t *fs,
                   const svn_fs_x__reps_t *container,
                   apr_size_t idx,
                   apr_pool_t *result_pool);

/* Let the EXTRACTOR object fetch all parts of the desired fulltext and
 * return the latter in *CONTENTS.  If SIZE is not 0, return SIZE bytes
 * starting at offset START_OFFSET of the full contents.  If that range
 * lies partly or completely outside the content, clip it accordingly.
 * Allocate the result in RESULT_POOL and use SCRATCH_POOL for temporary
 * allocations.
 *
 * Note, you may not run this inside a cache access function.
 */
svn_error_t *
svn_fs_x__extractor_drive(svn_stringbuf_t** contents,
                          svn_fs_x__rep_extractor_t* extractor,
                          apr_size_t start_offset,
                          apr_size_t size,
                          apr_pool_t* result_pool,
                          apr_pool_t* scratch_pool);

/* I/O interface. */

/* Write a serialized representation of the final container described by
 * BUILDER to STREAM.  Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__write_reps_container(svn_stream_t *stream,
                               const svn_fs_x__reps_builder_t *builder,
                               apr_pool_t *scratch_pool);

/* Read a representations container from its serialized representation in
 * STREAM.  Allocate the result in RESULT_POOL and return it in *CONTAINER.
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__read_reps_container(svn_fs_x__reps_t **container,
                              svn_stream_t *stream,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);

/* Implements #svn_cache__serialize_func_t for svn_fs_x__reps_t objects.
 */
svn_error_t *
svn_fs_x__serialize_reps_container(void **data,
                                   apr_size_t *data_len,
                                   void *in,
                                   apr_pool_t *pool);

/* Implements #svn_cache__deserialize_func_t for svn_fs_x__reps_t objects.
 */
svn_error_t *
svn_fs_x__deserialize_reps_container(void **out,
                                     void *data,
                                     apr_size_t data_len,
                                     apr_pool_t *result_pool);

/* Implements svn_cache__partial_getter_func_t for svn_fs_x__reps_t,
 * setting *OUT to an svn_fs_x__rep_extractor_t object defined by the
 * svn_fs_x__reps_baton_t passed in as *BATON.  This function is similar
 * to svn_fs_x__reps_get but operates on the cache serialized
 * representation of the container.
 */
svn_error_t *
svn_fs_x__reps_get_func(void **out,
                        const void *data,
                        apr_size_t data_len,
                        void *baton,
                        apr_pool_t *pool);

#endif
