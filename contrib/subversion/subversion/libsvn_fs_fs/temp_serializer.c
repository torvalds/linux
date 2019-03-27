/* temp_serializer.c: serialization functions for caching of FSFS structures
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

#include <apr_pools.h>

#include "svn_pools.h"
#include "svn_hash.h"
#include "svn_sorts.h"
#include "svn_fs.h"

#include "private/svn_fs_util.h"
#include "private/svn_sorts_private.h"
#include "private/svn_temp_serializer.h"
#include "private/svn_subr_private.h"

#include "id.h"
#include "temp_serializer.h"
#include "low_level.h"
#include "cached_data.h"

/* Utility to encode a signed NUMBER into a variable-length sequence of
 * 8-bit chars in KEY_BUFFER and return the last writen position.
 *
 * Numbers will be stored in 7 bits / byte and using byte values above
 * 32 (' ') to make them combinable with other string by simply separating
 * individual parts with spaces.
 */
static char*
encode_number(apr_int64_t number, char *key_buffer)
{
  /* encode the sign in the first byte */
  if (number < 0)
  {
    number = -number;
    *key_buffer = (char)((number & 63) + ' ' + 65);
  }
  else
    *key_buffer = (char)((number & 63) + ' ' + 1);
  number /= 64;

  /* write 7 bits / byte until no significant bits are left */
  while (number)
  {
    *++key_buffer = (char)((number & 127) + ' ' + 1);
    number /= 128;
  }

  /* return the last written position */
  return key_buffer;
}

const char*
svn_fs_fs__combine_number_and_string(apr_int64_t number,
                                     const char *string,
                                     apr_pool_t *pool)
{
  apr_size_t len = strlen(string);

  /* number part requires max. 10x7 bits + 1 space.
   * Add another 1 for the terminal 0 */
  char *key_buffer = apr_palloc(pool, len + 12);
  const char *key = key_buffer;

  /* Prepend the number to the string and separate them by space. No other
   * number can result in the same prefix, no other string in the same
   * postfix nor can the boundary between them be ambiguous. */
  key_buffer = encode_number(number, key_buffer);
  *++key_buffer = ' ';
  memcpy(++key_buffer, string, len+1);

  /* return the start of the key */
  return key;
}

/* Utility function to serialize string S in the given serialization CONTEXT.
 */
static void
serialize_svn_string(svn_temp_serializer__context_t *context,
                     const svn_string_t * const *s)
{
  const svn_string_t *string = *s;

  /* Nothing to do for NULL string references. */
  if (string == NULL)
    return;

  svn_temp_serializer__push(context, (const void * const *)s, sizeof(**s));

  /* the "string" content may actually be arbitrary binary data.
   * Thus, we cannot use svn_temp_serializer__add_string. */
  svn_temp_serializer__add_leaf(context,
                                (const void * const *)&string->data,
                                string->len + 1);

  /* back to the caller's nesting level */
  svn_temp_serializer__pop(context);
}

/* Utility function to deserialize the STRING inside the BUFFER.
 */
static void
deserialize_svn_string(void *buffer, svn_string_t **string)
{
  svn_temp_deserializer__resolve(buffer, (void **)string);
  if (*string == NULL)
    return;

  svn_temp_deserializer__resolve(*string, (void **)&(*string)->data);
}

/* Utility function to serialize the REPRESENTATION within the given
 * serialization CONTEXT.
 */
static void
serialize_representation(svn_temp_serializer__context_t *context,
                         representation_t * const *representation)
{
  const representation_t * rep = *representation;
  if (rep == NULL)
    return;

  /* serialize the representation struct itself */
  svn_temp_serializer__add_leaf(context,
                                (const void * const *)representation,
                                sizeof(**representation));
}

/* auxiliary structure representing the content of a directory array */
typedef struct dir_data_t
{
  /* number of entries in the directory
   * (it's int because the directory is an APR array) */
  int count;

  /** Current length of the in-txn in-disk representation of the directory.
   * SVN_INVALID_FILESIZE if unknown (i.e. committed data). */
  svn_filesize_t txn_filesize;

  /* number of unused dir entry buckets in the index */
  apr_size_t over_provision;

  /* internal modifying operations counter
   * (used to repack data once in a while) */
  apr_size_t operations;

  /* size of the serialization buffer actually used.
   * (we will allocate more than we actually need such that we may
   * append more data in situ later) */
  apr_size_t len;

  /* reference to the entries */
  svn_fs_dirent_t **entries;

  /* size of the serialized entries and don't be too wasteful
   * (needed since the entries are no longer in sequence) */
  apr_uint32_t *lengths;
} dir_data_t;

/* Utility function to serialize the *ENTRY_P into a the given
 * serialization CONTEXT. Return the serialized size of the
 * dir entry in *LENGTH.
 */
static void
serialize_dir_entry(svn_temp_serializer__context_t *context,
                    svn_fs_dirent_t **entry_p,
                    apr_uint32_t *length)
{
  svn_fs_dirent_t *entry = *entry_p;
  apr_size_t initial_length = svn_temp_serializer__get_length(context);

  svn_temp_serializer__push(context,
                            (const void * const *)entry_p,
                            sizeof(**entry_p));

  svn_fs_fs__id_serialize(context, &entry->id);
  svn_temp_serializer__add_string(context, &entry->name);

  *length = (apr_uint32_t)(  svn_temp_serializer__get_length(context)
                           - APR_ALIGN_DEFAULT(initial_length));

  svn_temp_serializer__pop(context);
}

/* Utility function to serialize the DIR into a new serialization
 * context to be returned. Allocation will be made form POOL.
 */
static svn_temp_serializer__context_t *
serialize_dir(svn_fs_fs__dir_data_t *dir, apr_pool_t *pool)
{
  dir_data_t dir_data;
  int i = 0;
  svn_temp_serializer__context_t *context;
  apr_array_header_t *entries = dir->entries;

  /* calculate sizes */
  int count = entries->nelts;
  apr_size_t over_provision = 2 + count / 4;
  apr_size_t total_count = count + over_provision;
  apr_size_t entries_len = total_count * sizeof(*dir_data.entries);
  apr_size_t lengths_len = total_count * sizeof(*dir_data.lengths);

  /* copy the hash entries to an auxiliary struct of known layout */
  dir_data.count = count;
  dir_data.txn_filesize = dir->txn_filesize;
  dir_data.over_provision = over_provision;
  dir_data.operations = 0;
  dir_data.entries = apr_palloc(pool, entries_len);
  dir_data.lengths = apr_palloc(pool, lengths_len);

  for (i = 0; i < count; ++i)
    dir_data.entries[i] = APR_ARRAY_IDX(entries, i, svn_fs_dirent_t *);

  /* Serialize that aux. structure into a new one. Also, provide a good
   * estimate for the size of the buffer that we will need. */
  context = svn_temp_serializer__init(&dir_data,
                                      sizeof(dir_data),
                                      50 + count * 200 + entries_len,
                                      pool);

  /* serialize entries references */
  svn_temp_serializer__push(context,
                            (const void * const *)&dir_data.entries,
                            entries_len);

  /* serialize the individual entries and their sub-structures */
  for (i = 0; i < count; ++i)
    serialize_dir_entry(context,
                        &dir_data.entries[i],
                        &dir_data.lengths[i]);

  svn_temp_serializer__pop(context);

  /* serialize entries references */
  svn_temp_serializer__push(context,
                            (const void * const *)&dir_data.lengths,
                            lengths_len);

  return context;
}

/* Utility function to reconstruct a dir entries struct from serialized data
 * in BUFFER and DIR_DATA. Allocation will be made form POOL.
 */
static svn_fs_fs__dir_data_t *
deserialize_dir(void *buffer, dir_data_t *dir_data, apr_pool_t *pool)
{
  svn_fs_fs__dir_data_t *result;
  apr_size_t i;
  apr_size_t count;
  svn_fs_dirent_t *entry;
  svn_fs_dirent_t **entries;

  /* Construct empty directory object. */
  result = apr_pcalloc(pool, sizeof(*result));
  result->entries
    = apr_array_make(pool, dir_data->count, sizeof(svn_fs_dirent_t *));
  result->txn_filesize = dir_data->txn_filesize;

  /* resolve the reference to the entries array */
  svn_temp_deserializer__resolve(buffer, (void **)&dir_data->entries);
  entries = dir_data->entries;

  /* fixup the references within each entry and add it to the RESULT */
  for (i = 0, count = dir_data->count; i < count; ++i)
    {
      svn_temp_deserializer__resolve(entries, (void **)&entries[i]);
      entry = dir_data->entries[i];

      /* pointer fixup */
      svn_temp_deserializer__resolve(entry, (void **)&entry->name);
      svn_fs_fs__id_deserialize(entry, (svn_fs_id_t **)&entry->id);

      /* add the entry to the hash */
      APR_ARRAY_PUSH(result->entries, svn_fs_dirent_t *) = entry;
    }

  /* return the now complete hash */
  return result;
}

void
svn_fs_fs__noderev_serialize(svn_temp_serializer__context_t *context,
                             node_revision_t * const *noderev_p)
{
  const node_revision_t *noderev = *noderev_p;
  if (noderev == NULL)
    return;

  /* serialize the representation struct itself */
  svn_temp_serializer__push(context,
                            (const void * const *)noderev_p,
                            sizeof(*noderev));

  /* serialize sub-structures */
  svn_fs_fs__id_serialize(context, &noderev->id);
  svn_fs_fs__id_serialize(context, &noderev->predecessor_id);
  serialize_representation(context, &noderev->prop_rep);
  serialize_representation(context, &noderev->data_rep);

  svn_temp_serializer__add_string(context, &noderev->copyfrom_path);
  svn_temp_serializer__add_string(context, &noderev->copyroot_path);
  svn_temp_serializer__add_string(context, &noderev->created_path);

  /* return to the caller's nesting level */
  svn_temp_serializer__pop(context);
}


void
svn_fs_fs__noderev_deserialize(void *buffer,
                               node_revision_t **noderev_p)
{
  node_revision_t *noderev;

  /* fixup the reference to the representation itself,
   * if this is part of a parent structure. */
  if (buffer != *noderev_p)
    svn_temp_deserializer__resolve(buffer, (void **)noderev_p);

  noderev = *noderev_p;
  if (noderev == NULL)
    return;

  /* fixup of sub-structures */
  svn_fs_fs__id_deserialize(noderev, (svn_fs_id_t **)&noderev->id);
  svn_fs_fs__id_deserialize(noderev, (svn_fs_id_t **)&noderev->predecessor_id);
  svn_temp_deserializer__resolve(noderev, (void **)&noderev->prop_rep);
  svn_temp_deserializer__resolve(noderev, (void **)&noderev->data_rep);

  svn_temp_deserializer__resolve(noderev, (void **)&noderev->copyfrom_path);
  svn_temp_deserializer__resolve(noderev, (void **)&noderev->copyroot_path);
  svn_temp_deserializer__resolve(noderev, (void **)&noderev->created_path);
}

svn_error_t *
svn_fs_fs__serialize_raw_window(void **buffer,
                                apr_size_t *buffer_size,
                                void *item,
                                apr_pool_t *pool)
{
  svn_fs_fs__raw_cached_window_t *window = item;
  svn_stringbuf_t *serialized;

  /* initialize the serialization process and allocate a buffer large
   * enough to do prevent re-allocations. */
  svn_temp_serializer__context_t *context =
      svn_temp_serializer__init(window,
                                sizeof(*window),
                                sizeof(*window) + window->window.len + 16,
                                pool);

  /* serialize the sub-structure(s) */
  svn_temp_serializer__add_leaf(context,
                                (const void * const *)&window->window.data,
                                window->window.len + 1);

  /* return the serialized result */
  serialized = svn_temp_serializer__get(context);

  *buffer = serialized->data;
  *buffer_size = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__deserialize_raw_window(void **item,
                                  void *buffer,
                                  apr_size_t buffer_size,
                                  apr_pool_t *pool)
{
  svn_fs_fs__raw_cached_window_t *window =
      (svn_fs_fs__raw_cached_window_t *)buffer;

  /* pointer reference fixup */
  svn_temp_deserializer__resolve(window, (void **)&window->window.data);

  /* done */
  *item = buffer;

  return SVN_NO_ERROR;
}


/* Utility function to serialize COUNT svn_txdelta_op_t objects
 * at OPS in the given serialization CONTEXT.
 */
static void
serialize_txdelta_ops(svn_temp_serializer__context_t *context,
                      const svn_txdelta_op_t * const * ops,
                      apr_size_t count)
{
  if (*ops == NULL)
    return;

  /* the ops form a contiguous chunk of memory with no further references */
  svn_temp_serializer__add_leaf(context,
                                (const void * const *)ops,
                                count * sizeof(**ops));
}

/* Utility function to serialize W in the given serialization CONTEXT.
 */
static void
serialize_txdeltawindow(svn_temp_serializer__context_t *context,
                        svn_txdelta_window_t * const * w)
{
  svn_txdelta_window_t *window = *w;

  /* serialize the window struct itself */
  svn_temp_serializer__push(context, (const void * const *)w, sizeof(**w));

  /* serialize its sub-structures */
  serialize_txdelta_ops(context, &window->ops, window->num_ops);
  serialize_svn_string(context, &window->new_data);

  svn_temp_serializer__pop(context);
}

svn_error_t *
svn_fs_fs__serialize_txdelta_window(void **buffer,
                                    apr_size_t *buffer_size,
                                    void *item,
                                    apr_pool_t *pool)
{
  svn_fs_fs__txdelta_cached_window_t *window_info = item;
  svn_stringbuf_t *serialized;

  /* initialize the serialization process and allocate a buffer large
   * enough to do without the need of re-allocations in most cases. */
  apr_size_t text_len = window_info->window->new_data
                      ? window_info->window->new_data->len
                      : 0;
  svn_temp_serializer__context_t *context =
      svn_temp_serializer__init(window_info,
                                sizeof(*window_info),
                                500 + text_len,
                                pool);

  /* serialize the sub-structure(s) */
  serialize_txdeltawindow(context, &window_info->window);

  /* return the serialized result */
  serialized = svn_temp_serializer__get(context);

  *buffer = serialized->data;
  *buffer_size = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__deserialize_txdelta_window(void **item,
                                      void *buffer,
                                      apr_size_t buffer_size,
                                      apr_pool_t *pool)
{
  svn_txdelta_window_t *window;

  /* Copy the _full_ buffer as it also contains the sub-structures. */
  svn_fs_fs__txdelta_cached_window_t *window_info =
      (svn_fs_fs__txdelta_cached_window_t *)buffer;

  /* pointer reference fixup */
  svn_temp_deserializer__resolve(window_info,
                                 (void **)&window_info->window);
  window = window_info->window;

  svn_temp_deserializer__resolve(window, (void **)&window->ops);

  deserialize_svn_string(window, (svn_string_t**)&window->new_data);

  /* done */
  *item = window_info;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__serialize_manifest(void **data,
                              apr_size_t *data_len,
                              void *in,
                              apr_pool_t *pool)
{
  apr_array_header_t *manifest = in;

  *data_len = sizeof(apr_off_t) *manifest->nelts;
  *data = apr_pmemdup(pool, manifest->elts, *data_len);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__deserialize_manifest(void **out,
                                void *data,
                                apr_size_t data_len,
                                apr_pool_t *pool)
{
  apr_array_header_t *manifest = apr_array_make(pool, 1, sizeof(apr_off_t));

  manifest->nelts = (int) (data_len / sizeof(apr_off_t));
  manifest->nalloc = (int) (data_len / sizeof(apr_off_t));
  manifest->elts = (char*)data;

  *out = manifest;

  return SVN_NO_ERROR;
}

/* Auxiliary structure representing the content of a properties hash.
   This structure is much easier to (de-)serialize than an apr_hash.
 */
typedef struct properties_data_t
{
  /* number of entries in the hash */
  apr_size_t count;

  /* reference to the keys */
  const char **keys;

  /* reference to the values */
  const svn_string_t **values;
} properties_data_t;

/* Serialize COUNT C-style strings from *STRINGS into CONTEXT. */
static void
serialize_cstring_array(svn_temp_serializer__context_t *context,
                        const char ***strings,
                        apr_size_t count)
{
  apr_size_t i;
  const char **entries = *strings;

  /* serialize COUNT entries pointers (the array) */
  svn_temp_serializer__push(context,
                            (const void * const *)strings,
                            count * sizeof(const char*));

  /* serialize array elements */
  for (i = 0; i < count; ++i)
    svn_temp_serializer__add_string(context, &entries[i]);

  svn_temp_serializer__pop(context);
}

/* Serialize COUNT svn_string_t* items from *STRINGS into CONTEXT. */
static void
serialize_svn_string_array(svn_temp_serializer__context_t *context,
                           const svn_string_t ***strings,
                           apr_size_t count)
{
  apr_size_t i;
  const svn_string_t **entries = *strings;

  /* serialize COUNT entries pointers (the array) */
  svn_temp_serializer__push(context,
                            (const void * const *)strings,
                            count * sizeof(const char*));

  /* serialize array elements */
  for (i = 0; i < count; ++i)
    serialize_svn_string(context, &entries[i]);

  svn_temp_serializer__pop(context);
}

svn_error_t *
svn_fs_fs__serialize_properties(void **data,
                                apr_size_t *data_len,
                                void *in,
                                apr_pool_t *pool)
{
  apr_hash_t *hash = in;
  properties_data_t properties;
  svn_temp_serializer__context_t *context;
  apr_hash_index_t *hi;
  svn_stringbuf_t *serialized;
  apr_size_t i;

  /* create our auxiliary data structure */
  properties.count = apr_hash_count(hash);
  properties.keys = apr_palloc(pool, sizeof(const char*) * (properties.count + 1));
  properties.values = apr_palloc(pool, sizeof(const svn_string_t *) * properties.count);

  /* populate it with the hash entries */
  for (hi = apr_hash_first(pool, hash), i=0; hi; hi = apr_hash_next(hi), ++i)
    {
      properties.keys[i] = apr_hash_this_key(hi);
      properties.values[i] = apr_hash_this_val(hi);
    }

  /* serialize it */
  context = svn_temp_serializer__init(&properties,
                                      sizeof(properties),
                                      properties.count * 100,
                                      pool);

  properties.keys[i] = "";
  serialize_cstring_array(context, &properties.keys, properties.count + 1);
  serialize_svn_string_array(context, &properties.values, properties.count);

  /* return the serialized result */
  serialized = svn_temp_serializer__get(context);

  *data = serialized->data;
  *data_len = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__deserialize_properties(void **out,
                                  void *data,
                                  apr_size_t data_len,
                                  apr_pool_t *pool)
{
  apr_hash_t *hash = svn_hash__make(pool);
  properties_data_t *properties = (properties_data_t *)data;
  size_t i;

  /* de-serialize our auxiliary data structure */
  svn_temp_deserializer__resolve(properties, (void**)&properties->keys);
  svn_temp_deserializer__resolve(properties, (void**)&properties->values);

  /* de-serialize each entry and put it into the hash */
  for (i = 0; i < properties->count; ++i)
    {
      apr_size_t len = properties->keys[i+1] - properties->keys[i] - 1;
      svn_temp_deserializer__resolve(properties->keys,
                                     (void**)&properties->keys[i]);

      deserialize_svn_string(properties->values,
                             (svn_string_t **)&properties->values[i]);

      apr_hash_set(hash,
                   properties->keys[i], len,
                   properties->values[i]);
    }

  /* done */
  *out = hash;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__serialize_revprops(void **data,
                              apr_size_t *data_len,
                              void *in,
                              apr_pool_t *pool)
{
  svn_string_t *buffer = in;

  *data = (void *)buffer->data;
  *data_len = buffer->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__deserialize_revprops(void **out,
                                void *data,
                                apr_size_t data_len,
                                apr_pool_t *pool)
{
  apr_hash_t *properties;
  svn_stream_t *stream;

  svn_string_t buffer;
  buffer.data = data;
  buffer.len = data_len;

  stream = svn_stream_from_string(&buffer, pool);
  properties = svn_hash__make(pool);

  SVN_ERR(svn_hash_read2(properties, stream, SVN_HASH_TERMINATOR, pool));

  /* done */
  *out = properties;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__serialize_id(void **data,
                        apr_size_t *data_len,
                        void *in,
                        apr_pool_t *pool)
{
  const svn_fs_id_t *id = in;
  svn_stringbuf_t *serialized;

  /* create an (empty) serialization context with plenty of buffer space */
  svn_temp_serializer__context_t *context =
      svn_temp_serializer__init(NULL, 0, 250, pool);

  /* serialize the id */
  svn_fs_fs__id_serialize(context, &id);

  /* return serialized data */
  serialized = svn_temp_serializer__get(context);
  *data = serialized->data;
  *data_len = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__deserialize_id(void **out,
                          void *data,
                          apr_size_t data_len,
                          apr_pool_t *pool)
{
  /* Copy the _full_ buffer as it also contains the sub-structures. */
  svn_fs_id_t *id = (svn_fs_id_t *)data;

  /* fixup of all pointers etc. */
  svn_fs_fs__id_deserialize(id, &id);

  /* done */
  *out = id;
  return SVN_NO_ERROR;
}

/** Caching node_revision_t objects. **/

svn_error_t *
svn_fs_fs__serialize_node_revision(void **buffer,
                                   apr_size_t *buffer_size,
                                   void *item,
                                   apr_pool_t *pool)
{
  svn_stringbuf_t *serialized;
  node_revision_t *noderev = item;

  /* create an (empty) serialization context with plenty of (initial)
   * buffer space. */
  svn_temp_serializer__context_t *context =
      svn_temp_serializer__init(NULL, 0,
                                1024 - SVN_TEMP_SERIALIZER__OVERHEAD,
                                pool);

  /* serialize the noderev */
  svn_fs_fs__noderev_serialize(context, &noderev);

  /* return serialized data */
  serialized = svn_temp_serializer__get(context);
  *buffer = serialized->data;
  *buffer_size = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__deserialize_node_revision(void **item,
                                     void *buffer,
                                     apr_size_t buffer_size,
                                     apr_pool_t *pool)
{
  /* Copy the _full_ buffer as it also contains the sub-structures. */
  node_revision_t *noderev = (node_revision_t *)buffer;

  /* fixup of all pointers etc. */
  svn_fs_fs__noderev_deserialize(noderev, &noderev);

  /* done */
  *item = noderev;
  return SVN_NO_ERROR;
}

/* Utility function that returns the directory serialized inside CONTEXT
 * to DATA and DATA_LEN.  If OVERPROVISION is set, allocate some extra
 * room for future in-place changes by svn_fs_fs__replace_dir_entry. */
static svn_error_t *
return_serialized_dir_context(svn_temp_serializer__context_t *context,
                              void **data,
                              apr_size_t *data_len,
                              svn_boolean_t overprovision)
{
  svn_stringbuf_t *serialized = svn_temp_serializer__get(context);

  *data = serialized->data;
  *data_len = overprovision ? serialized->blocksize : serialized->len;
  ((dir_data_t *)serialized->data)->len = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__serialize_dir_entries(void **data,
                                 apr_size_t *data_len,
                                 void *in,
                                 apr_pool_t *pool)
{
  svn_fs_fs__dir_data_t *dir = in;

  /* serialize the dir content into a new serialization context
   * and return the serialized data */
  return return_serialized_dir_context(serialize_dir(dir, pool),
                                       data,
                                       data_len,
                                       FALSE);
}

svn_error_t *
svn_fs_fs__serialize_txndir_entries(void **data,
                                    apr_size_t *data_len,
                                    void *in,
                                    apr_pool_t *pool)
{
  svn_fs_fs__dir_data_t *dir = in;

  /* serialize the dir content into a new serialization context
   * and return the serialized data */
  return return_serialized_dir_context(serialize_dir(dir, pool),
                                       data,
                                       data_len,
                                       TRUE);
}

svn_error_t *
svn_fs_fs__deserialize_dir_entries(void **out,
                                   void *data,
                                   apr_size_t data_len,
                                   apr_pool_t *pool)
{
  /* Copy the _full_ buffer as it also contains the sub-structures. */
  dir_data_t *dir_data = (dir_data_t *)data;

  /* reconstruct the hash from the serialized data */
  *out = deserialize_dir(dir_data, dir_data, pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_sharded_offset(void **out,
                              const void *data,
                              apr_size_t data_len,
                              void *baton,
                              apr_pool_t *pool)
{
  const apr_off_t *manifest = data;
  apr_int64_t shard_pos = *(apr_int64_t *)baton;

  *(apr_off_t *)out = manifest[shard_pos];

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__extract_dir_filesize(void **out,
                                const void *data,
                                apr_size_t data_len,
                                void *baton,
                                apr_pool_t *pool)
{
  const dir_data_t *dir_data = data;

  *(svn_filesize_t *)out = dir_data->txn_filesize;

  return SVN_NO_ERROR;
}

/* Utility function that returns the lowest index of the first entry in
 * *ENTRIES that points to a dir entry with a name equal or larger than NAME.
 * If an exact match has been found, *FOUND will be set to TRUE. COUNT is
 * the number of valid entries in ENTRIES.
 */
static apr_size_t
find_entry(svn_fs_dirent_t **entries,
           const char *name,
           apr_size_t count,
           svn_boolean_t *found)
{
  /* binary search for the desired entry by name */
  apr_size_t lower = 0;
  apr_size_t upper = count;
  apr_size_t middle;

  for (middle = upper / 2; lower < upper; middle = (upper + lower) / 2)
    {
      const svn_fs_dirent_t *entry =
          svn_temp_deserializer__ptr(entries, (const void *const *)&entries[middle]);
      const char* entry_name =
          svn_temp_deserializer__ptr(entry, (const void *const *)&entry->name);

      int diff = strcmp(entry_name, name);
      if (diff < 0)
        lower = middle + 1;
      else
        upper = middle;
    }

  /* check whether we actually found a match */
  *found = FALSE;
  if (lower < count)
    {
      const svn_fs_dirent_t *entry =
          svn_temp_deserializer__ptr(entries, (const void *const *)&entries[lower]);
      const char* entry_name =
          svn_temp_deserializer__ptr(entry, (const void *const *)&entry->name);

      if (strcmp(entry_name, name) == 0)
        *found = TRUE;
    }

  return lower;
}

svn_error_t *
svn_fs_fs__extract_dir_entry(void **out,
                             const void *data,
                             apr_size_t data_len,
                             void *baton,
                             apr_pool_t *pool)
{
  const dir_data_t *dir_data = data;
  extract_dir_entry_baton_t *entry_baton = baton;
  svn_boolean_t found;

  /* resolve the reference to the entries array */
  const svn_fs_dirent_t * const *entries =
    svn_temp_deserializer__ptr(data, (const void *const *)&dir_data->entries);

  /* resolve the reference to the lengths array */
  const apr_uint32_t *lengths =
    svn_temp_deserializer__ptr(data, (const void *const *)&dir_data->lengths);

  /* binary search for the desired entry by name */
  apr_size_t pos = find_entry((svn_fs_dirent_t **)entries,
                              entry_baton->name,
                              dir_data->count,
                              &found);

  /* de-serialize that entry or return NULL, if no match has been found.
   * Be sure to check that the directory contents is still up-to-date. */
  entry_baton->out_of_date
    = dir_data->txn_filesize != entry_baton->txn_filesize;

  *out = NULL;
  if (found && !entry_baton->out_of_date)
    {
      const svn_fs_dirent_t *source =
          svn_temp_deserializer__ptr(entries, (const void *const *)&entries[pos]);

      /* Entries have been serialized one-by-one, each time including all
       * nested structures and strings. Therefore, they occupy a single
       * block of memory whose end-offset is either the beginning of the
       * next entry or the end of the buffer
       */
      apr_size_t size = lengths[pos];

      /* copy & deserialize the entry */
      svn_fs_dirent_t *new_entry = apr_pmemdup(pool, source, size);

      svn_temp_deserializer__resolve(new_entry, (void **)&new_entry->name);
      svn_fs_fs__id_deserialize(new_entry, (svn_fs_id_t **)&new_entry->id);
      *(svn_fs_dirent_t **)out = new_entry;
    }

  return SVN_NO_ERROR;
}

/* Utility function for svn_fs_fs__replace_dir_entry that implements the
 * modification as a simply deserialize / modify / serialize sequence.
 */
static svn_error_t *
slowly_replace_dir_entry(void **data,
                         apr_size_t *data_len,
                         void *baton,
                         apr_pool_t *pool)
{
  replace_baton_t *replace_baton = (replace_baton_t *)baton;
  dir_data_t *dir_data = (dir_data_t *)*data;
  svn_fs_fs__dir_data_t *dir;
  int idx = -1;
  svn_fs_dirent_t *entry;
  apr_array_header_t *entries;

  SVN_ERR(svn_fs_fs__deserialize_dir_entries((void **)&dir,
                                             *data,
                                             dir_data->len,
                                             pool));

  entries = dir->entries;
  entry = svn_fs_fs__find_dir_entry(entries, replace_baton->name, &idx);

  /* Replacement or removal? */
  if (replace_baton->new_entry)
    {
      /* Replace ENTRY with / insert the NEW_ENTRY */
      if (entry)
        APR_ARRAY_IDX(entries, idx, svn_fs_dirent_t *)
          = replace_baton->new_entry;
      else
        svn_sort__array_insert(entries, &replace_baton->new_entry, idx);
    }
  else
    {
      /* Remove the old ENTRY. */
      if (entry)
        svn_sort__array_delete(entries, idx, 1);
    }

  return svn_fs_fs__serialize_dir_entries(data, data_len, dir, pool);
}

svn_error_t *
svn_fs_fs__replace_dir_entry(void **data,
                             apr_size_t *data_len,
                             void *baton,
                             apr_pool_t *pool)
{
  replace_baton_t *replace_baton = (replace_baton_t *)baton;
  dir_data_t *dir_data = (dir_data_t *)*data;
  svn_boolean_t found;
  svn_fs_dirent_t **entries;
  apr_uint32_t *lengths;
  apr_uint32_t length;
  apr_size_t pos;

  svn_temp_serializer__context_t *context;

  /* update the cached file length info.
   * Because we are writing to the cache, it is fair to assume that the
   * caller made sure that the current contents is consistent with the
   * previous state of the directory file. */
  dir_data->txn_filesize = replace_baton->txn_filesize;

  /* after quite a number of operations, let's re-pack everything.
   * This is to limit the number of wasted space as we cannot overwrite
   * existing data but must always append. */
  if (dir_data->operations > 2 + dir_data->count / 4)
    return slowly_replace_dir_entry(data, data_len, baton, pool);

  /* resolve the reference to the entries array */
  entries = (svn_fs_dirent_t **)
    svn_temp_deserializer__ptr(dir_data,
                               (const void *const *)&dir_data->entries);

  /* resolve the reference to the lengths array */
  lengths = (apr_uint32_t *)
    svn_temp_deserializer__ptr(dir_data,
                               (const void *const *)&dir_data->lengths);

  /* binary search for the desired entry by name */
  pos = find_entry(entries, replace_baton->name, dir_data->count, &found);

  /* handle entry removal (if found at all) */
  if (replace_baton->new_entry == NULL)
    {
      if (found)
        {
          /* remove reference to the entry from the index */
          memmove(&entries[pos],
                  &entries[pos + 1],
                  sizeof(entries[pos]) * (dir_data->count - pos));
          memmove(&lengths[pos],
                  &lengths[pos + 1],
                  sizeof(lengths[pos]) * (dir_data->count - pos));

          dir_data->count--;
          dir_data->over_provision++;
          dir_data->operations++;
        }

      return SVN_NO_ERROR;
    }

  /* if not found, prepare to insert the new entry */
  if (!found)
    {
      /* fallback to slow operation if there is no place left to insert an
       * new entry to index. That will automatically give add some spare
       * entries ("overprovision"). */
      if (dir_data->over_provision == 0)
        return slowly_replace_dir_entry(data, data_len, baton, pool);

      /* make entries[index] available for pointing to the new entry */
      memmove(&entries[pos + 1],
              &entries[pos],
              sizeof(entries[pos]) * (dir_data->count - pos));
      memmove(&lengths[pos + 1],
              &lengths[pos],
              sizeof(lengths[pos]) * (dir_data->count - pos));

      dir_data->count++;
      dir_data->over_provision--;
      dir_data->operations++;
    }

  /* de-serialize the new entry */
  entries[pos] = replace_baton->new_entry;
  context = svn_temp_serializer__init_append(dir_data,
                                             entries,
                                             dir_data->len,
                                             *data_len,
                                             pool);
  serialize_dir_entry(context, &entries[pos], &length);

  /* return the updated serialized data */
  SVN_ERR(return_serialized_dir_context(context, data, data_len, TRUE));

  /* since the previous call may have re-allocated the buffer, the lengths
   * pointer may no longer point to the entry in that buffer. Therefore,
   * re-map it again and store the length value after that. */

  dir_data = (dir_data_t *)*data;
  lengths = (apr_uint32_t *)
    svn_temp_deserializer__ptr(dir_data,
                               (const void *const *)&dir_data->lengths);
  lengths[pos] = length;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__reset_txn_filesize(void **data,
                              apr_size_t *data_len,
                              void *baton,
                              apr_pool_t *pool)
{
  dir_data_t *dir_data = (dir_data_t *)*data;
  dir_data->txn_filesize = SVN_INVALID_FILESIZE;

  return SVN_NO_ERROR;
}

svn_error_t  *
svn_fs_fs__serialize_rep_header(void **data,
                                apr_size_t *data_len,
                                void *in,
                                apr_pool_t *pool)
{
  svn_fs_fs__rep_header_t *copy = apr_palloc(pool, sizeof(*copy));
  *copy = *(svn_fs_fs__rep_header_t *)in;

  *data_len = sizeof(*copy);
  *data = copy;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__deserialize_rep_header(void **out,
                                  void *data,
                                  apr_size_t data_len,
                                  apr_pool_t *pool)
{
  svn_fs_fs__rep_header_t *copy = apr_palloc(pool, sizeof(*copy));
  SVN_ERR_ASSERT(data_len == sizeof(*copy));

  *copy = *(svn_fs_fs__rep_header_t *)data;
  *out = data;

  return SVN_NO_ERROR;
}

/* Utility function to serialize change CHANGE_P in the given serialization
 * CONTEXT.
 */
static void
serialize_change(svn_temp_serializer__context_t *context,
                 change_t * const *change_p)
{
  const change_t * change = *change_p;
  if (change == NULL)
    return;

  /* serialize the change struct itself */
  svn_temp_serializer__push(context,
                            (const void * const *)change_p,
                            sizeof(*change));

  /* serialize sub-structures */
  svn_fs_fs__id_serialize(context, &change->info.node_rev_id);

  svn_temp_serializer__add_string(context, &change->path.data);
  svn_temp_serializer__add_string(context, &change->info.copyfrom_path);

  /* return to the caller's nesting level */
  svn_temp_serializer__pop(context);
}

/* Utility function to serialize the CHANGE_P within the given
 * serialization CONTEXT.
 */
static void
deserialize_change(void *buffer, change_t **change_p)
{
  change_t * change;

  /* fix-up of the pointer to the struct in question */
  svn_temp_deserializer__resolve(buffer, (void **)change_p);

  change = *change_p;
  if (change == NULL)
    return;

  /* fix-up of sub-structures */
  svn_fs_fs__id_deserialize(change, (svn_fs_id_t **)&change->info.node_rev_id);

  svn_temp_deserializer__resolve(change, (void **)&change->path.data);
  svn_temp_deserializer__resolve(change, (void **)&change->info.copyfrom_path);
}

svn_error_t *
svn_fs_fs__serialize_changes(void **data,
                             apr_size_t *data_len,
                             void *in,
                             apr_pool_t *pool)
{
  svn_fs_fs__changes_list_t *changes = in;
  svn_temp_serializer__context_t *context;
  svn_stringbuf_t *serialized;
  int i;

  /* serialize it and all its elements */
  context = svn_temp_serializer__init(changes,
                                      sizeof(*changes),
                                      changes->count * 250,
                                      pool);

  svn_temp_serializer__push(context,
                            (const void * const *)&changes->changes,
                            changes->count * sizeof(*changes->changes));

  for (i = 0; i < changes->count; ++i)
    serialize_change(context, &changes->changes[i]);

  svn_temp_serializer__pop(context);

  /* return the serialized result */
  serialized = svn_temp_serializer__get(context);

  *data = serialized->data;
  *data_len = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__deserialize_changes(void **out,
                               void *data,
                               apr_size_t data_len,
                               apr_pool_t *pool)
{
  int i;
  svn_fs_fs__changes_list_t *changes = (svn_fs_fs__changes_list_t *)data;

  /* de-serialize our auxiliary data structure */
  svn_temp_deserializer__resolve(changes, (void**)&changes->changes);

  /* de-serialize each entry and add it to the array */
  for (i = 0; i < changes->count; ++i)
    deserialize_change(changes->changes,
                       (change_t **)&changes->changes[i]);

  /* done */
  *out = changes;

  return SVN_NO_ERROR;
}

/* Auxiliary structure representing the content of a svn_mergeinfo_t hash.
   This structure is much easier to (de-)serialize than an APR array.
 */
typedef struct mergeinfo_data_t
{
  /* number of paths in the hash */
  unsigned count;

  /* COUNT keys (paths) */
  const char **keys;

  /* COUNT keys lengths (strlen of path) */
  apr_ssize_t *key_lengths;

  /* COUNT entries, each giving the number of ranges for the key */
  int *range_counts;

  /* all ranges in a single, concatenated buffer */
  svn_merge_range_t *ranges;
} mergeinfo_data_t;

svn_error_t *
svn_fs_fs__serialize_mergeinfo(void **data,
                               apr_size_t *data_len,
                               void *in,
                               apr_pool_t *pool)
{
  svn_mergeinfo_t mergeinfo = in;
  mergeinfo_data_t merges;
  svn_temp_serializer__context_t *context;
  svn_stringbuf_t *serialized;
  apr_hash_index_t *hi;
  unsigned i;
  int k;
  apr_size_t range_count;

  /* initialize our auxiliary data structure */
  merges.count = apr_hash_count(mergeinfo);
  merges.keys = apr_palloc(pool, sizeof(*merges.keys) * merges.count);
  merges.key_lengths = apr_palloc(pool, sizeof(*merges.key_lengths) *
                                        merges.count);
  merges.range_counts = apr_palloc(pool, sizeof(*merges.range_counts) *
                                         merges.count);

  i = 0;
  range_count = 0;
  for (hi = apr_hash_first(pool, mergeinfo); hi; hi = apr_hash_next(hi), ++i)
    {
      svn_rangelist_t *ranges;
      apr_hash_this(hi, (const void**)&merges.keys[i],
                        &merges.key_lengths[i],
                        (void **)&ranges);
      merges.range_counts[i] = ranges->nelts;
      range_count += ranges->nelts;
    }

  merges.ranges = apr_palloc(pool, sizeof(*merges.ranges) * range_count);

  i = 0;
  for (hi = apr_hash_first(pool, mergeinfo); hi; hi = apr_hash_next(hi))
    {
      svn_rangelist_t *ranges = apr_hash_this_val(hi);
      for (k = 0; k < ranges->nelts; ++k, ++i)
        merges.ranges[i] = *APR_ARRAY_IDX(ranges, k, svn_merge_range_t*);
    }

  /* serialize it and all its elements */
  context = svn_temp_serializer__init(&merges,
                                      sizeof(merges),
                                      range_count * 30,
                                      pool);

  /* keys array */
  svn_temp_serializer__push(context,
                            (const void * const *)&merges.keys,
                            merges.count * sizeof(*merges.keys));

  for (i = 0; i < merges.count; ++i)
    svn_temp_serializer__add_string(context, &merges.keys[i]);

  svn_temp_serializer__pop(context);

  /* key lengths array */
  svn_temp_serializer__add_leaf(context,
                                (const void * const *)&merges.key_lengths,
                                merges.count * sizeof(*merges.key_lengths));

  /* range counts array */
  svn_temp_serializer__add_leaf(context,
                                (const void * const *)&merges.range_counts,
                                merges.count * sizeof(*merges.range_counts));

  /* ranges */
  svn_temp_serializer__add_leaf(context,
                                (const void * const *)&merges.ranges,
                                range_count * sizeof(*merges.ranges));

  /* return the serialized result */
  serialized = svn_temp_serializer__get(context);

  *data = serialized->data;
  *data_len = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__deserialize_mergeinfo(void **out,
                                 void *data,
                                 apr_size_t data_len,
                                 apr_pool_t *pool)
{
  unsigned i;
  int k, n;
  mergeinfo_data_t *merges = (mergeinfo_data_t *)data;
  svn_mergeinfo_t mergeinfo;

  /* de-serialize our auxiliary data structure */
  svn_temp_deserializer__resolve(merges, (void**)&merges->keys);
  svn_temp_deserializer__resolve(merges, (void**)&merges->key_lengths);
  svn_temp_deserializer__resolve(merges, (void**)&merges->range_counts);
  svn_temp_deserializer__resolve(merges, (void**)&merges->ranges);

  /* de-serialize keys and add entries to the result */
  n = 0;
  mergeinfo = svn_hash__make(pool);
  for (i = 0; i < merges->count; ++i)
    {
      svn_rangelist_t *ranges = apr_array_make(pool,
                                               merges->range_counts[i],
                                               sizeof(svn_merge_range_t*));
      for (k = 0; k < merges->range_counts[i]; ++k, ++n)
        APR_ARRAY_PUSH(ranges, svn_merge_range_t*) = &merges->ranges[n];

      svn_temp_deserializer__resolve(merges->keys,
                                     (void**)&merges->keys[i]);
      apr_hash_set(mergeinfo, merges->keys[i], merges->key_lengths[i], ranges);
    }

  /* done */
  *out = mergeinfo;

  return SVN_NO_ERROR;
}

