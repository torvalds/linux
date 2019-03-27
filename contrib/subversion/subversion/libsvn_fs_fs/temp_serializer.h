/* temp_serializer.h : serialization functions for caching of FSFS structures
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

#ifndef SVN_LIBSVN_FS__TEMP_SERIALIZER_H
#define SVN_LIBSVN_FS__TEMP_SERIALIZER_H

#include "fs.h"

/**
 * Prepend the @a number to the @a string in a space efficient way such that
 * no other (number,string) combination can produce the same result.
 * Allocate temporaries as well as the result from @a pool.
 */
const char*
svn_fs_fs__combine_number_and_string(apr_int64_t number,
                                     const char *string,
                                     apr_pool_t *pool);

/**
 * Serialize a @a noderev_p within the serialization @a context.
 */
void
svn_fs_fs__noderev_serialize(struct svn_temp_serializer__context_t *context,
                             node_revision_t * const *noderev_p);

/**
 * Deserialize a @a noderev_p within the @a buffer.
 */
void
svn_fs_fs__noderev_deserialize(void *buffer,
                               node_revision_t **noderev_p);


/**
 * Adds position information to the raw window data in WINDOW.
 */
typedef struct
{
  /* the (unprocessed) txdelta window byte sequence cached / to be cached */
  svn_string_t window;

  /* the offset within the representation right after reading the window */
  apr_off_t end_offset;

  /* svndiff version */
  int ver;
} svn_fs_fs__raw_cached_window_t;

/**
 * Implements #svn_cache__serialize_func_t for
 * #svn_fs_fs__raw_cached_window_t.
 */
svn_error_t *
svn_fs_fs__serialize_raw_window(void **buffer,
                                apr_size_t *buffer_size,
                                void *item,
                                apr_pool_t *pool);

/**
 * Implements #svn_cache__deserialize_func_t for
 * #svn_fs_fs__raw_cached_window_t.
 */
svn_error_t *
svn_fs_fs__deserialize_raw_window(void **item,
                                  void *buffer,
                                  apr_size_t buffer_size,
                                  apr_pool_t *pool);

/**
 * #svn_txdelta_window_t is not sufficient for caching the data it
 * represents because data read process needs auxiliary information.
 */
typedef struct
{
  /* the txdelta window information cached / to be cached */
  svn_txdelta_window_t *window;

  /* the revision file read pointer position right after reading the window */
  apr_off_t end_offset;
} svn_fs_fs__txdelta_cached_window_t;

/**
 * Implements #svn_cache__serialize_func_t for
 * #svn_fs_fs__txdelta_cached_window_t.
 */
svn_error_t *
svn_fs_fs__serialize_txdelta_window(void **buffer,
                                    apr_size_t *buffer_size,
                                    void *item,
                                    apr_pool_t *pool);

/**
 * Implements #svn_cache__deserialize_func_t for
 * #svn_fs_fs__txdelta_cached_window_t.
 */
svn_error_t *
svn_fs_fs__deserialize_txdelta_window(void **item,
                                      void *buffer,
                                      apr_size_t buffer_size,
                                      apr_pool_t *pool);

/**
 * Implements #svn_cache__serialize_func_t for a manifest
 * (@a in is an #apr_array_header_t of apr_off_t elements).
 */
svn_error_t *
svn_fs_fs__serialize_manifest(void **data,
                              apr_size_t *data_len,
                              void *in,
                              apr_pool_t *pool);

/**
 * Implements #svn_cache__deserialize_func_t for a manifest
 * (@a *out is an #apr_array_header_t of apr_off_t elements).
 */
svn_error_t *
svn_fs_fs__deserialize_manifest(void **out,
                                void *data,
                                apr_size_t data_len,
                                apr_pool_t *pool);

/**
 * Implements #svn_cache__serialize_func_t for a properties hash
 * (@a in is an #apr_hash_t of svn_string_t elements, keyed by const char*).
 */
svn_error_t *
svn_fs_fs__serialize_properties(void **data,
                                apr_size_t *data_len,
                                void *in,
                                apr_pool_t *pool);

/**
 * Implements #svn_cache__deserialize_func_t for a properties hash
 * (@a *out is an #apr_hash_t of svn_string_t elements, keyed by const char*).
 */
svn_error_t *
svn_fs_fs__deserialize_properties(void **out,
                                  void *data,
                                  apr_size_t data_len,
                                  apr_pool_t *pool);

/**
 * Implements #svn_cache__serialize_func_t for a properties hash
 * (@a in is an #apr_hash_t of svn_string_t elements, keyed by const char*).
 */
svn_error_t *
svn_fs_fs__serialize_revprops(void **data,
                              apr_size_t *data_len,
                              void *in,
                              apr_pool_t *pool);

/**
 * Implements #svn_cache__deserialize_func_t for a properties hash
 * (@a *out is an #apr_hash_t of svn_string_t elements, keyed by const char*).
 */
svn_error_t *
svn_fs_fs__deserialize_revprops(void **out,
                                void *data,
                                apr_size_t data_len,
                                apr_pool_t *pool);

/**
 * Implements #svn_cache__serialize_func_t for #svn_fs_id_t
 */
svn_error_t *
svn_fs_fs__serialize_id(void **data,
                        apr_size_t *data_len,
                        void *in,
                        apr_pool_t *pool);

/**
 * Implements #svn_cache__deserialize_func_t for #svn_fs_id_t
 */
svn_error_t *
svn_fs_fs__deserialize_id(void **out,
                          void *data,
                          apr_size_t data_len,
                          apr_pool_t *pool);

/**
 * Implements #svn_cache__serialize_func_t for #node_revision_t
 */
svn_error_t *
svn_fs_fs__serialize_node_revision(void **buffer,
                                   apr_size_t *buffer_size,
                                   void *item,
                                   apr_pool_t *pool);

/**
 * Implements #svn_cache__deserialize_func_t for #node_revision_t
 */
svn_error_t *
svn_fs_fs__deserialize_node_revision(void **item,
                                     void *buffer,
                                     apr_size_t buffer_size,
                                     apr_pool_t *pool);

/**
 * Implements #svn_cache__serialize_func_t for a #svn_fs_fs__dir_data_t
 */
svn_error_t *
svn_fs_fs__serialize_dir_entries(void **data,
                                 apr_size_t *data_len,
                                 void *in,
                                 apr_pool_t *pool);

/**
 * Same as svn_fs_fs__serialize_dir_entries but allocates extra room for
 * in-place modification.
 */
svn_error_t *
svn_fs_fs__serialize_txndir_entries(void **data,
                                    apr_size_t *data_len,
                                    void *in,
                                    apr_pool_t *pool);

/**
 * Implements #svn_cache__deserialize_func_t for a #svn_fs_fs__dir_data_t
 */
svn_error_t *
svn_fs_fs__deserialize_dir_entries(void **out,
                                   void *data,
                                   apr_size_t data_len,
                                   apr_pool_t *pool);

/**
 * Implements #svn_cache__partial_getter_func_t.  Set (apr_off_t) @a *out
 * to the element indexed by (apr_int64_t) @a *baton within the
 * serialized manifest array @a data and @a data_len. */
svn_error_t *
svn_fs_fs__get_sharded_offset(void **out,
                              const void *data,
                              apr_size_t data_len,
                              void *baton,
                              apr_pool_t *pool);

/**
 * Implements #svn_cache__partial_getter_func_t.
 * Set (svn_filesize_t) @a *out to the filesize info stored with the
 * serialized directory in @a data of @a data_len.  @a baton is unused.
 */
svn_error_t *
svn_fs_fs__extract_dir_filesize(void **out,
                                const void *data,
                                apr_size_t data_len,
                                void *baton,
                                apr_pool_t *pool);

/**
 * Describes the entry to be found in a directory: Identifies the entry
 * by @a name and requires the directory file size to be @a filesize.
 */
typedef struct extract_dir_entry_baton_t
{
  /** name of the directory entry to return */
  const char *name;

  /** Current length of the in-txn in-disk representation of the directory.
   * SVN_INVALID_FILESIZE if unknown. */
  svn_filesize_t txn_filesize;

  /** Will be set by the callback.  If FALSE, the cached data is out of date.
   * We need this indicator because the svn_cache__t interface will always
   * report the lookup as a success (FOUND==TRUE) if the generic lookup was
   * successful -- regardless of what the entry extraction callback does. */
  svn_boolean_t out_of_date;
} extract_dir_entry_baton_t;


/**
 * Implements #svn_cache__partial_getter_func_t for a single
 * #svn_fs_dirent_t within a serialized directory contents hash,
 * identified by its name (in (extract_dir_entry_baton_t *) @a *baton).
 * If the filesize specified in the baton does not match the cached
 * value for this directory, @a *out will be NULL as well.
 */
svn_error_t *
svn_fs_fs__extract_dir_entry(void **out,
                             const void *data,
                             apr_size_t data_len,
                             void *baton,
                             apr_pool_t *pool);

/**
 * Describes the change to be done to a directory: Set the entry
 * identify by @a name to the value @a new_entry. If the latter is
 * @c NULL, the entry shall be removed if it exists. Otherwise it
 * will be replaced or automatically added, respectively.  The
 * @a filesize allows readers to identify stale cache data (e.g.
 * due to concurrent access to txns); writers use it to update the
 * cached file size info.
 */
typedef struct replace_baton_t
{
  /** name of the directory entry to modify */
  const char *name;

  /** directory entry to insert instead */
  svn_fs_dirent_t *new_entry;

  /** Current length of the in-txn in-disk representation of the directory.
   * SVN_INVALID_FILESIZE if unknown. */
  svn_filesize_t txn_filesize;
} replace_baton_t;

/**
 * Implements #svn_cache__partial_setter_func_t for a single
 * #svn_fs_dirent_t within a serialized directory contents hash,
 * identified by its name in the #replace_baton_t in @a baton.
 */
svn_error_t *
svn_fs_fs__replace_dir_entry(void **data,
                             apr_size_t *data_len,
                             void *baton,
                             apr_pool_t *pool);

/**
 * Implements #svn_cache__partial_setter_func_t for a #svn_fs_fs__dir_data_t
 * at @a *data, resetting its txn_filesize field to SVN_INVALID_FILESIZE.
 * &a baton should be NULL.
 */
svn_error_t *
svn_fs_fs__reset_txn_filesize(void **data,
                              apr_size_t *data_len,
                              void *baton,
                              apr_pool_t *pool);

/**
 * Implements #svn_cache__serialize_func_t for a #svn_fs_fs__rep_header_t.
 */
svn_error_t *
svn_fs_fs__serialize_rep_header(void **data,
                                apr_size_t *data_len,
                                void *in,
                                apr_pool_t *pool);

/**
 * Implements #svn_cache__deserialize_func_t for a #svn_fs_fs__rep_header_t.
 */
svn_error_t *
svn_fs_fs__deserialize_rep_header(void **out,
                                  void *data,
                                  apr_size_t data_len,
                                  apr_pool_t *pool);

/*** Block of changes in a changed paths list. */
typedef struct svn_fs_fs__changes_list_t
{
  /* Offset of the first element in CHANGES within the changed paths list
     on disk. */
  apr_off_t start_offset;

  /* Offset of the first element behind CHANGES within the changed paths
     list on disk. */
  apr_off_t end_offset;

  /* End of list reached? This may have false negatives in case the number
     of elements in the list is a multiple of our block / range size. */
  svn_boolean_t eol;

  /* Array of #svn_fs_x__change_t * representing a consecutive sub-range of
     elements in a changed paths list. */

  /* number of entries in the array */
  int count;

  /* reference to the changes */
  change_t **changes;

} svn_fs_fs__changes_list_t;

/**
 * Implements #svn_cache__serialize_func_t for a #svn_fs_fs__changes_list_t.
 */
svn_error_t *
svn_fs_fs__serialize_changes(void **data,
                             apr_size_t *data_len,
                             void *in,
                             apr_pool_t *pool);

/**
 * Implements #svn_cache__deserialize_func_t for a #svn_fs_fs__changes_list_t.
 */
svn_error_t *
svn_fs_fs__deserialize_changes(void **out,
                               void *data,
                               apr_size_t data_len,
                               apr_pool_t *pool);

/**
 * Implements #svn_cache__serialize_func_t for #svn_mergeinfo_t objects.
 */
svn_error_t *
svn_fs_fs__serialize_mergeinfo(void **data,
                               apr_size_t *data_len,
                               void *in,
                               apr_pool_t *pool);

/**
 * Implements #svn_cache__deserialize_func_t for #svn_mergeinfo_t objects.
 */
svn_error_t *
svn_fs_fs__deserialize_mergeinfo(void **out,
                                 void *data,
                                 apr_size_t data_len,
                                 apr_pool_t *pool);

#endif
