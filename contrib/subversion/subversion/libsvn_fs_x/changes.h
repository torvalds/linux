/* changes.h --- FSX changed paths lists container
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

#ifndef SVN_LIBSVN_FS_X_CHANGES_H
#define SVN_LIBSVN_FS_X_CHANGES_H

#include "svn_io.h"
#include "fs.h"

/* Entries in a revision's change list tend to be widely redundant (similar
 * changes to similar paths).  Even more so, change lists from a larger
 * revision range also tend to overlap.
 *
 * In its serialized form, the svn_fs_x__changes_t container extracts most
 * of that redundancy and the run-time representation is also much smaller
 * than sum of the respective svn_fs_x__change_t* arrays.
 *
 * As with other containers, this one has two modes: 'construction', in
 * which you may add data to it, and 'getter' in which there is only r/o
 * access to the data.
 */

/* An opaque collection of change lists (apr_array_header_t * of
 * svn_fs_x__change_t *).
 */
typedef struct svn_fs_x__changes_t svn_fs_x__changes_t;

/* Create and populate changes containers. */

/* Create and return a new changes container with an initial capacity of
 * INITIAL_COUNT svn_fs_x__change_t objects.
 * Allocate the result in RESULT_POOL.
 */
svn_fs_x__changes_t *
svn_fs_x__changes_create(apr_size_t initial_count,
                         apr_pool_t *result_pool);

/* Start a new change list CHANGES (implicitly terminating the previous one)
 * and return its index in *LIST_INDEX.  Append all changes from LIST to
 * that new change list.
 */
svn_error_t *
svn_fs_x__changes_append_list(apr_size_t *list_index,
                              svn_fs_x__changes_t *changes,
                              apr_array_header_t *list);

/* Return a rough estimate in bytes for the serialized representation
 * of CHANGES.
 */
apr_size_t
svn_fs_x__changes_estimate_size(const svn_fs_x__changes_t *changes);

/* Read changes containers. */

/* From CHANGES, access the change list with the given IDX and extract the
 * next entries according to CONTEXT.  Allocate the result in RESULT_POOL
 * and return it in *LIST.
 */
svn_error_t *
svn_fs_x__changes_get_list(apr_array_header_t **list,
                           const svn_fs_x__changes_t *changes,
                           apr_size_t idx,
                           svn_fs_x__changes_context_t *context,
                           apr_pool_t *result_pool);

/* I/O interface. */

/* Write a serialized representation of CHANGES to STREAM.
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__write_changes_container(svn_stream_t *stream,
                                  const svn_fs_x__changes_t *changes,
                                  apr_pool_t *scratch_pool);

/* Read a changes container from its serialized representation in STREAM.
 * Allocate the result in RESULT_POOL and return it in *CHANGES_P.  Use
 * SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__read_changes_container(svn_fs_x__changes_t **changes_p,
                                 svn_stream_t *stream,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

/* Implements #svn_cache__serialize_func_t for svn_fs_x__changes_t objects.
 */
svn_error_t *
svn_fs_x__serialize_changes_container(void **data,
                                      apr_size_t *data_len,
                                      void *in,
                                      apr_pool_t *pool);

/* Implements #svn_cache__deserialize_func_t for svn_fs_x__changes_t objects.
 */
svn_error_t *
svn_fs_x__deserialize_changes_container(void **out,
                                        void *data,
                                        apr_size_t data_len,
                                        apr_pool_t *result_pool);

/* Baton type to be used with svn_fs_x__changes_get_list_func. */
typedef struct svn_fs_x__changes_get_list_baton_t
{
  /* Sub-item to query */
  apr_uint32_t sub_item;

  /* Deliver data starting from this index within the changes list. */
  int start;

  /* To be set by svn_fs_x__changes_get_list_func:
     Did we deliver the last change in that list? */
  svn_boolean_t *eol;
} svn_fs_x__changes_get_list_baton_t;

/* Implements svn_cache__partial_getter_func_t for svn_fs_x__changes_t,
 * setting *OUT to the change list (apr_array_header_t *) selected by
 * the svn_fs_x__changes_get_list_baton_t passed in as *BATON.  This
 * function is similar to svn_fs_x__changes_get_list but operates on
 * the cache serialized representation of the container.
 */
svn_error_t *
svn_fs_x__changes_get_list_func(void **out,
                                const void *data,
                                apr_size_t data_len,
                                void *baton,
                                apr_pool_t *pool);

#endif
