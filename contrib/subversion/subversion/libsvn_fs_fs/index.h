/* index.h : interface to FSFS indexing functionality
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

#ifndef SVN_LIBSVN_FS__INDEX_H
#define SVN_LIBSVN_FS__INDEX_H

#include "fs.h"
#include "rev_file.h"

/* Per-defined item index values.  They are used to identify empty or
 * mandatory items.
 */
#define SVN_FS_FS__ITEM_INDEX_UNUSED     0  /* invalid / reserved value */
#define SVN_FS_FS__ITEM_INDEX_CHANGES    1  /* list of changed paths */
#define SVN_FS_FS__ITEM_INDEX_ROOT_NODE  2  /* the root noderev */
#define SVN_FS_FS__ITEM_INDEX_FIRST_USER 3  /* first noderev to be freely
                                               assigned */

/* Data / item types as stored in the phys-to-log index.
 */
#define SVN_FS_FS__ITEM_TYPE_UNUSED     0  /* file section not used */
#define SVN_FS_FS__ITEM_TYPE_FILE_REP   1  /* item is a file representation */
#define SVN_FS_FS__ITEM_TYPE_DIR_REP    2  /* item is a directory rep. */
#define SVN_FS_FS__ITEM_TYPE_FILE_PROPS 3  /* item is a file property rep. */
#define SVN_FS_FS__ITEM_TYPE_DIR_PROPS  4  /* item is a directory prop rep */
#define SVN_FS_FS__ITEM_TYPE_NODEREV    5  /* item is a noderev */
#define SVN_FS_FS__ITEM_TYPE_CHANGES    6  /* item is a changed paths list */

#define SVN_FS_FS__ITEM_TYPE_ANY_REP    7  /* item is any representation.
                                              Only used in pre-format7. */

/* Open / create a log-to-phys index file with the full file path name
 * FILE_NAME.  Return the open file in *PROTO_INDEX allocated in
 * RESULT_POOL.
 */
svn_error_t *
svn_fs_fs__l2p_proto_index_open(apr_file_t **proto_index,
                                const char *file_name,
                                apr_pool_t *result_pool);

/* Call this function before adding entries for the next revision to the
 * log-to-phys index file in PROTO_INDEX.  Use SCRATCH_POOL for temporary
 * allocations.
 */
svn_error_t *
svn_fs_fs__l2p_proto_index_add_revision(apr_file_t *proto_index,
                                        apr_pool_t *scratch_pool);

/* Add a new mapping, ITEM_INDEX to the OFFSET, to log-to-phys index file
 * in PROTO_INDEX.  Please note that mappings may be added in any order
 * but duplicate entries for the same ITEM_INDEX are not supported.
 * Not all possible index values need to be used.  OFFSET may be -1 to
 * mark 'invalid' item indexes but that is already implied for all item
 * indexes not explicitly given a mapping.
 *
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__l2p_proto_index_add_entry(apr_file_t *proto_index,
                                     apr_off_t offset,
                                     apr_uint64_t item_index,
                                     apr_pool_t *scratch_pool);

/* Use the proto index file stored at PROTO_FILE_NAME, construct the final
 * log-to-phys index and append it to INDEX_FILE.  The first revision will
 * be REVISION, entries to the next revision will be assigned to REVISION+1
 * and so forth.
 *
 * Return the MD5 checksum of the on-disk index data in *CHECKSUM, allocated
 * in RESULT_POOL.  Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__l2p_index_append(svn_checksum_t **checksum,
                            svn_fs_t *fs,
                            apr_file_t *index_file,
                            const char *proto_file_name,
                            svn_revnum_t revision,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/* Open / create a phys-to-log index file with the full file path name
 * FILE_NAME.  Return the open file in *PROTO_INDEX allocated in
 * RESULT_POOL.
 */
svn_error_t *
svn_fs_fs__p2l_proto_index_open(apr_file_t **proto_index,
                                const char *file_name,
                                apr_pool_t *result_pool);

/* Add a new mapping ENTRY to the phys-to-log index file in PROTO_INDEX.
 * The entries must be added in ascending offset order and must not leave
 * intermittent ranges uncovered.  The revision value in ENTRY may be
 * SVN_INVALID_REVISION.  Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__p2l_proto_index_add_entry(apr_file_t *proto_index,
                                     const svn_fs_fs__p2l_entry_t *entry,
                                     apr_pool_t *scratch_pool);

/* Set *NEXT_OFFSET to the first offset behind the last entry in the
 * phys-to-log proto index file PROTO_INDEX.  This will be 0 for empty
 * index files.  Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__p2l_proto_index_next_offset(apr_off_t *next_offset,
                                       apr_file_t *proto_index,
                                       apr_pool_t *scratch_pool);

/* Use the proto index file stored at PROTO_FILE_NAME, construct the final
 * phys-to-log index and append it to INDEX_FILE.  Entries without a valid
 * revision will be assigned to the REVISION given here.
 *
 * Return the MD5 checksum of the on-disk index data in *CHECKSUM, allocated
 * in RESULT_POOL.  Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__p2l_index_append(svn_checksum_t **checksum,
                            svn_fs_t *fs,
                            apr_file_t *index_file,
                            const char *proto_file_name,
                            svn_revnum_t revision,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/* Use the phys-to-log mapping files in FS to build a list of entries
 * that (at least partly) overlap with the range given by BLOCK_START
 * offset and BLOCK_SIZE in the rep / pack file containing REVISION.
 * Return the array in *ENTRIES with svn_fs_fs__p2l_entry_t as elements,
 * allocated in RESULT_POOL.  REV_FILE determines whether to access single
 * rev or pack file data.  If that is not available anymore (neither in
 * cache nor on disk), return an error.  Use SCRATCH_POOL for temporary
 * allocations.
 *
 * Note that (only) the first and the last mapping may cross a cluster
 * boundary.
 */
svn_error_t *
svn_fs_fs__p2l_index_lookup(apr_array_header_t **entries,
                            svn_fs_t *fs,
                            svn_fs_fs__revision_file_t *rev_file,
                            svn_revnum_t revision,
                            apr_off_t block_start,
                            apr_off_t block_size,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/* Use the phys-to-log mapping files in FS to return the entry for the
 * item starting at global OFFSET in the rep file containing REVISION in
 * *ENTRY, allocated in RESULT_POOL.  Sets *ENTRY to NULL if no item starts
 * at exactly that offset.  REV_FILE determines whether to access single
 * rev or pack file data.  If that is not available anymore (neither in
 * cache nor on disk), return an error.  Use SCRATCH_POOL for temporary
 * allocations.
 */
svn_error_t *
svn_fs_fs__p2l_entry_lookup(svn_fs_fs__p2l_entry_t **entry,
                            svn_fs_t *fs,
                            svn_fs_fs__revision_file_t *rev_file,
                            svn_revnum_t revision,
                            apr_off_t offset,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/* For ITEM_INDEX within REV in FS, return the position in the respective
 * rev or pack file in *ABSOLUTE_POSITION.  If TXN_ID is not NULL, return
 * the file offset within that transaction and REV should be given as
 * SVN_INVALID_REVNUM in that case.
 *
 * REV_FILE determines whether to access single rev or pack file data.
 * If that is not available anymore (neither in cache nor on disk), re-open
 * the rev / pack file and retry to open the index file.  For anything but
 * committed log addressed revisions, REV_FILE may be NULL.
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__item_offset(apr_off_t *absolute_position,
                       svn_fs_t *fs,
                       svn_fs_fs__revision_file_t *rev_file,
                       svn_revnum_t revision,
                       const svn_fs_fs__id_part_t *txn_id,
                       apr_uint64_t item_index,
                       apr_pool_t *scratch_pool);

/* Use the log-to-phys indexes in FS to determine the maximum item indexes
 * assigned to revision START_REV to START_REV + COUNT - 1.  That is a
 * close upper limit to the actual number of items in the respective revs.
 * Return the results in *MAX_IDS,  allocated in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__l2p_get_max_ids(apr_array_header_t **max_ids,
                           svn_fs_t *fs,
                           svn_revnum_t start_rev,
                           apr_size_t count,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/* In *OFFSET, return the last OFFSET in the pack / rev file containing.
 * REV_FILE determines whether to access single rev or pack file data.
 * If that is not available anymore (neither in cache nor on disk), re-open
 * the rev / pack file and retry to open the index file.
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__p2l_get_max_offset(apr_off_t *offset,
                              svn_fs_t *fs,
                              svn_fs_fs__revision_file_t *rev_file,
                              svn_revnum_t revision,
                              apr_pool_t *scratch_pool);

/* Index (re-)creation utilities.
 */

/* For FS, create a new L2P auto-deleting proto index file in POOL and return
 * its name in *PROTONAME.  All entries to write are given in ENTRIES and
 * entries are of type svn_fs_fs__p2l_entry_t* (sic!).  The ENTRIES array
 * will be reordered.  Give the proto index file the lifetime of RESULT_POOL
 * and use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__l2p_index_from_p2l_entries(const char **protoname,
                                      svn_fs_t *fs,
                                      apr_array_header_t *entries,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool);

/* For FS, create a new P2L auto-deleting proto index file in POOL and return
 * its name in *PROTONAME.  All entries to write are given in ENTRIES and
 * of type svn_fs_fs__p2l_entry_t*.  The FVN1 checksums are not taken from
 * ENTRIES but are begin calculated from the current contents of REV_FILE
 * as we go.  Give the proto index file the lifetime of RESULT_POOL and use
 * SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__p2l_index_from_p2l_entries(const char **protoname,
                                      svn_fs_t *fs,
                                      svn_fs_fs__revision_file_t *rev_file,
                                      apr_array_header_t *entries,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool);

/* Serialization and caching interface
 */

/* We use this key type to address individual pages from both index types.
 */
typedef struct svn_fs_fs__page_cache_key_t
{
  /* in l2p: this is the revision of the items being mapped
     in p2l: this is the start revision identifying the pack / rev file */
  apr_uint32_t revision;

  /* if TRUE, this is the index to a pack file
   */
  svn_boolean_t is_packed;

  /* in l2p: page number within the revision
   * in p2l: page number with the rev / pack file
   */
  apr_uint64_t page;
} svn_fs_fs__page_cache_key_t;

/*
 * Implements svn_cache__serialize_func_t for l2p_header_t objects.
 */
svn_error_t *
svn_fs_fs__serialize_l2p_header(void **data,
                                apr_size_t *data_len,
                                void *in,
                                apr_pool_t *pool);

/*
 * Implements svn_cache__deserialize_func_t for l2p_header_t objects.
 */
svn_error_t *
svn_fs_fs__deserialize_l2p_header(void **out,
                                  void *data,
                                  apr_size_t data_len,
                                  apr_pool_t *pool);

/*
 * Implements svn_cache__serialize_func_t for l2p_page_t objects.
 */
svn_error_t *
svn_fs_fs__serialize_l2p_page(void **data,
                              apr_size_t *data_len,
                              void *in,
                              apr_pool_t *pool);

/*
 * Implements svn_cache__deserialize_func_t for l2p_page_t objects.
 */
svn_error_t *
svn_fs_fs__deserialize_l2p_page(void **out,
                                void *data,
                                apr_size_t data_len,
                                apr_pool_t *pool);

/*
 * Implements svn_cache__serialize_func_t for p2l_header_t objects.
 */
svn_error_t *
svn_fs_fs__serialize_p2l_header(void **data,
                                apr_size_t *data_len,
                                void *in,
                                apr_pool_t *pool);

/*
 * Implements svn_cache__deserialize_func_t for p2l_header_t objects.
 */
svn_error_t *
svn_fs_fs__deserialize_p2l_header(void **out,
                                  void *data,
                                  apr_size_t data_len,
                                  apr_pool_t *pool);

/*
 * Implements svn_cache__serialize_func_t for apr_array_header_t objects
 * with elements of type svn_fs_fs__p2l_entry_t.
 */
svn_error_t *
svn_fs_fs__serialize_p2l_page(void **data,
                              apr_size_t *data_len,
                              void *in,
                              apr_pool_t *pool);

/*
 * Implements svn_cache__deserialize_func_t for apr_array_header_t objects
 * with elements of type svn_fs_fs__p2l_entry_t.
 */
svn_error_t *
svn_fs_fs__deserialize_p2l_page(void **out,
                                void *data,
                                apr_size_t data_len,
                                apr_pool_t *pool);

#endif
