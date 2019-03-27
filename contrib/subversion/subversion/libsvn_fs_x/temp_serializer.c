/* temp_serializer.c: serialization functions for caching of FSX structures
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
svn_fs_x__combine_number_and_string(apr_int64_t number,
                                    const char *string,
                                    apr_pool_t *result_pool)
{
  apr_size_t len = strlen(string);

  /* number part requires max. 10x7 bits + 1 space.
   * Add another 1 for the terminal 0 */
  char *key_buffer = apr_palloc(result_pool, len + 12);
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

  svn_temp_serializer__push(context,
                            (const void * const *)s,
                            sizeof(*string));

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
deserialize_svn_string(const void *buffer, svn_string_t **string)
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
                         svn_fs_x__representation_t * const *representation)
{
  const svn_fs_x__representation_t * rep = *representation;
  if (rep == NULL)
    return;

  /* serialize the representation struct itself */
  svn_temp_serializer__add_leaf(context,
                                (const void * const *)representation,
                                sizeof(*rep));
}

void
svn_fs_x__serialize_apr_array(svn_temp_serializer__context_t *context,
                              apr_array_header_t **a)
{
  const apr_array_header_t *array = *a;

  /* Nothing to do for NULL string references. */
  if (array == NULL)
    return;

  /* array header struct */
  svn_temp_serializer__push(context,
                            (const void * const *)a,
                            sizeof(*array));

  /* contents */
  svn_temp_serializer__add_leaf(context,
                                (const void * const *)&array->elts,
                                (apr_size_t)array->nelts * array->elt_size);

  /* back to the caller's nesting level */
  svn_temp_serializer__pop(context);
}

void
svn_fs_x__deserialize_apr_array(void *buffer,
                                apr_array_header_t **array,
                                apr_pool_t *result_pool)
{
  svn_temp_deserializer__resolve(buffer, (void **)array);
  if (*array == NULL)
    return;

  svn_temp_deserializer__resolve(*array, (void **)&(*array)->elts);
  (*array)->pool = result_pool;
}

/* auxilliary structure representing the content of a directory array */
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
  svn_fs_x__dirent_t **entries;

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
                    svn_fs_x__dirent_t **entry_p,
                    apr_uint32_t *length)
{
  svn_fs_x__dirent_t *entry = *entry_p;
  apr_size_t initial_length = svn_temp_serializer__get_length(context);

  svn_temp_serializer__push(context,
                            (const void * const *)entry_p,
                            sizeof(**entry_p));

  svn_temp_serializer__add_string(context, &entry->name);

  *length = (apr_uint32_t)(  svn_temp_serializer__get_length(context)
                           - APR_ALIGN_DEFAULT(initial_length));

  svn_temp_serializer__pop(context);
}

/* Utility function to serialize the DIR into a new serialization
 * context to be returned.
 *
 * Temporary allocation will be made form SCRATCH_POOL.
 */
static svn_temp_serializer__context_t *
serialize_dir(svn_fs_x__dir_data_t *dir,
              apr_pool_t *scratch_pool)
{
  dir_data_t dir_data;
  int i = 0;
  svn_temp_serializer__context_t *context;
  apr_array_header_t *entries = dir->entries;

  /* calculate sizes */
  int count = entries->nelts;
  apr_size_t over_provision = 2 + count / 4;
  apr_size_t entries_len =   (count + over_provision)
                           * sizeof(svn_fs_x__dirent_t*);
  apr_size_t lengths_len = (count + over_provision) * sizeof(apr_uint32_t);

  /* Estimate the size of a directory entry + its name. */
  enum { ENTRY_SIZE = sizeof(svn_fs_x__dirent_t) + 32 };

  /* copy the hash entries to an auxiliary struct of known layout */
  dir_data.count = count;
  dir_data.txn_filesize = dir->txn_filesize;
  dir_data.over_provision = over_provision;
  dir_data.operations = 0;
  dir_data.entries = apr_palloc(scratch_pool, entries_len);
  dir_data.lengths = apr_palloc(scratch_pool, lengths_len);

  for (i = 0; i < count; ++i)
    dir_data.entries[i] = APR_ARRAY_IDX(entries, i, svn_fs_x__dirent_t *);

  /* Serialize that aux. structure into a new one. Also, provide a good
   * estimate for the size of the buffer that we will need. */
  context = svn_temp_serializer__init(&dir_data,
                                      sizeof(dir_data),
                                      50 + count * ENTRY_SIZE
                                         + entries_len + lengths_len,
                                      scratch_pool);

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
 * in BUFFER and DIR_DATA. Allocation will be made form RESULT_POOL.
 */
static svn_fs_x__dir_data_t *
deserialize_dir(void *buffer,
                dir_data_t *dir_data,
                apr_pool_t *result_pool)
{
  svn_fs_x__dir_data_t *result;
  apr_size_t i;
  apr_size_t count;
  svn_fs_x__dirent_t *entry;
  svn_fs_x__dirent_t **entries;

  /* Construct empty directory object. */
  result = apr_pcalloc(result_pool, sizeof(*result));
  result->entries
    = apr_array_make(result_pool, dir_data->count,
                     sizeof(svn_fs_x__dirent_t *));
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

      /* add the entry to the hash */
      APR_ARRAY_PUSH(result->entries, svn_fs_x__dirent_t *) = entry;
    }

  /* return the now complete hash */
  return result;
}

/**
 * Serialize a NODEREV_P within the serialization CONTEXT.
 */
static void
noderev_serialize(svn_temp_serializer__context_t *context,
                  svn_fs_x__noderev_t * const *noderev_p)
{
  const svn_fs_x__noderev_t *noderev = *noderev_p;
  if (noderev == NULL)
    return;

  /* serialize the representation struct itself */
  svn_temp_serializer__push(context,
                            (const void * const *)noderev_p,
                            sizeof(*noderev));

  /* serialize sub-structures */
  serialize_representation(context, &noderev->prop_rep);
  serialize_representation(context, &noderev->data_rep);

  svn_temp_serializer__add_string(context, &noderev->copyfrom_path);
  svn_temp_serializer__add_string(context, &noderev->copyroot_path);
  svn_temp_serializer__add_string(context, &noderev->created_path);

  /* return to the caller's nesting level */
  svn_temp_serializer__pop(context);
}

/**
 * Deserialize a NODEREV_P within the BUFFER and associate it with.
 */
static void
noderev_deserialize(void *buffer,
                    svn_fs_x__noderev_t **noderev_p)
{
  svn_fs_x__noderev_t *noderev;

  /* fixup the reference to the representation itself,
   * if this is part of a parent structure. */
  if (buffer != *noderev_p)
    svn_temp_deserializer__resolve(buffer, (void **)noderev_p);

  noderev = *noderev_p;
  if (noderev == NULL)
    return;

  /* fixup of sub-structures */
  svn_temp_deserializer__resolve(noderev, (void **)&noderev->prop_rep);
  svn_temp_deserializer__resolve(noderev, (void **)&noderev->data_rep);

  svn_temp_deserializer__resolve(noderev, (void **)&noderev->copyfrom_path);
  svn_temp_deserializer__resolve(noderev, (void **)&noderev->copyroot_path);
  svn_temp_deserializer__resolve(noderev, (void **)&noderev->created_path);
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
                                count * sizeof(svn_txdelta_op_t));
}

/* Utility function to serialize W in the given serialization CONTEXT.
 */
static void
serialize_txdeltawindow(svn_temp_serializer__context_t *context,
                        svn_txdelta_window_t * const * w)
{
  svn_txdelta_window_t *window = *w;

  /* serialize the window struct itself */
  svn_temp_serializer__push(context,
                            (const void * const *)w,
                            sizeof(svn_txdelta_window_t));

  /* serialize its sub-structures */
  serialize_txdelta_ops(context, &window->ops, window->num_ops);
  serialize_svn_string(context, &window->new_data);

  svn_temp_serializer__pop(context);
}

svn_error_t *
svn_fs_x__serialize_txdelta_window(void **buffer,
                                   apr_size_t *buffer_size,
                                   void *item,
                                   apr_pool_t *pool)
{
  svn_fs_x__txdelta_cached_window_t *window_info = item;
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
svn_fs_x__deserialize_txdelta_window(void **item,
                                     void *buffer,
                                     apr_size_t buffer_size,
                                     apr_pool_t *result_pool)
{
  svn_txdelta_window_t *window;

  /* Copy the _full_ buffer as it also contains the sub-structures. */
  svn_fs_x__txdelta_cached_window_t *window_info =
      (svn_fs_x__txdelta_cached_window_t *)buffer;

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
svn_fs_x__serialize_properties(void **data,
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
svn_fs_x__deserialize_properties(void **out,
                                 void *data,
                                 apr_size_t data_len,
                                 apr_pool_t *result_pool)
{
  apr_hash_t *hash = svn_hash__make(result_pool);
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

/** Caching svn_fs_x__noderev_t objects. **/

svn_error_t *
svn_fs_x__serialize_node_revision(void **buffer,
                                  apr_size_t *buffer_size,
                                  void *item,
                                  apr_pool_t *pool)
{
  svn_stringbuf_t *serialized;
  svn_fs_x__noderev_t *noderev = item;

  /* create an (empty) serialization context with plenty of (initial)
   * buffer space. */
  svn_temp_serializer__context_t *context =
      svn_temp_serializer__init(NULL, 0,
                                1024 - SVN_TEMP_SERIALIZER__OVERHEAD,
                                pool);

  /* serialize the noderev */
  noderev_serialize(context, &noderev);

  /* return serialized data */
  serialized = svn_temp_serializer__get(context);
  *buffer = serialized->data;
  *buffer_size = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__deserialize_node_revision(void **item,
                                    void *buffer,
                                    apr_size_t buffer_size,
                                    apr_pool_t *result_pool)
{
  /* Copy the _full_ buffer as it also contains the sub-structures. */
  svn_fs_x__noderev_t *noderev = (svn_fs_x__noderev_t *)buffer;

  /* fixup of all pointers etc. */
  noderev_deserialize(noderev, &noderev);

  /* done */
  *item = noderev;
  return SVN_NO_ERROR;
}

/* Utility function that returns the directory serialized inside CONTEXT
 * to DATA and DATA_LEN.  If OVERPROVISION is set, allocate some extra
 * room for future in-place changes by svn_fs_x__replace_dir_entry. */
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
svn_fs_x__serialize_dir_entries(void **data,
                                apr_size_t *data_len,
                                void *in,
                                apr_pool_t *pool)
{
  svn_fs_x__dir_data_t *dir = in;

  /* serialize the dir content into a new serialization context
   * and return the serialized data */
  return return_serialized_dir_context(serialize_dir(dir, pool),
                                       data,
                                       data_len,
                                       FALSE);
}

svn_error_t *
svn_fs_x__deserialize_dir_entries(void **out,
                                  void *data,
                                  apr_size_t data_len,
                                  apr_pool_t *result_pool)
{
  /* Copy the _full_ buffer as it also contains the sub-structures. */
  dir_data_t *dir_data = (dir_data_t *)data;

  /* reconstruct the hash from the serialized data */
  *out = deserialize_dir(dir_data, dir_data, result_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__get_sharded_offset(void **out,
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
svn_fs_x__extract_dir_filesize(void **out,
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
find_entry(svn_fs_x__dirent_t **entries,
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
      const svn_fs_x__dirent_t *entry =
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
      const svn_fs_x__dirent_t *entry =
          svn_temp_deserializer__ptr(entries, (const void *const *)&entries[lower]);
      const char* entry_name =
          svn_temp_deserializer__ptr(entry, (const void *const *)&entry->name);

      if (strcmp(entry_name, name) == 0)
        *found = TRUE;
    }

  return lower;
}

/* Utility function that returns TRUE if entry number IDX in ENTRIES has the
 * name NAME.
 */
static svn_boolean_t
found_entry(const svn_fs_x__dirent_t * const *entries,
            const char *name,
            apr_size_t idx)
{
  /* check whether we actually found a match */
  const svn_fs_x__dirent_t *entry =
    svn_temp_deserializer__ptr(entries, (const void *const *)&entries[idx]);
  const char* entry_name =
    svn_temp_deserializer__ptr(entry, (const void *const *)&entry->name);

  return strcmp(entry_name, name) == 0;
}

svn_error_t *
svn_fs_x__extract_dir_entry(void **out,
                            const void *data,
                            apr_size_t data_len,
                            void *baton,
                            apr_pool_t *pool)
{
  const dir_data_t *dir_data = data;
  svn_fs_x__ede_baton_t *b = baton;
  svn_boolean_t found;
  apr_size_t pos;

  /* resolve the reference to the entries array */
  const svn_fs_x__dirent_t * const *entries =
    svn_temp_deserializer__ptr(data, (const void *const *)&dir_data->entries);

  /* resolve the reference to the lengths array */
  const apr_uint32_t *lengths =
    svn_temp_deserializer__ptr(data, (const void *const *)&dir_data->lengths);

  /* Before we return, make sure we tell the caller this data is even still
     relevant. */
  b->out_of_date = dir_data->txn_filesize != b->txn_filesize;

  /* Special case: Early out for empty directories.
     That simplifies tests further down the road. */
  *out = NULL;
  if (dir_data->count == 0)
    return SVN_NO_ERROR;

  /* HINT _might_ be the position we hit last time.
     If within valid range, check whether HINT+1 is a hit. */
  if (   b->hint < dir_data->count - 1
      && found_entry(entries, b->name, b->hint + 1))
    {
      /* Got lucky. */
      pos = b->hint + 1;
      found = TRUE;
    }
  else
    {
      /* Binary search for the desired entry by name. */
      pos = find_entry((svn_fs_x__dirent_t **)entries, b->name,
                       dir_data->count, &found);
    }

  /* Remember the hit index - if we FOUND the entry. */
  if (found)
    b->hint = pos;

  /* de-serialize that entry or return NULL, if no match has been found.
   * Be sure to check that the directory contents is still up-to-date. */
  if (found && !b->out_of_date)
    {
      const svn_fs_x__dirent_t *source =
          svn_temp_deserializer__ptr(entries, (const void *const *)&entries[pos]);

      /* Entries have been serialized one-by-one, each time including all
       * nested structures and strings. Therefore, they occupy a single
       * block of memory whose end-offset is either the beginning of the
       * next entry or the end of the buffer
       */
      apr_size_t size = lengths[pos];

      /* copy & deserialize the entry */
      svn_fs_x__dirent_t *new_entry = apr_pmemdup(pool, source, size);

      svn_temp_deserializer__resolve(new_entry, (void **)&new_entry->name);
      *(svn_fs_x__dirent_t **)out = new_entry;
    }

  return SVN_NO_ERROR;
}

/* Utility function for svn_fs_x__replace_dir_entry that implements the
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
  svn_fs_x__dir_data_t *dir;
  int idx = -1;
  svn_fs_x__dirent_t *entry;
  apr_array_header_t *entries;

  SVN_ERR(svn_fs_x__deserialize_dir_entries((void **)&dir,
                                            *data,
                                            dir_data->len,
                                            pool));

  entries = dir->entries;
  entry = svn_fs_x__find_dir_entry(entries, replace_baton->name, &idx);

  /* Replacement or removal? */
  if (replace_baton->new_entry)
    {
      /* Replace ENTRY with / insert the NEW_ENTRY */
      if (entry)
        APR_ARRAY_IDX(entries, idx, svn_fs_x__dirent_t *)
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

  return svn_fs_x__serialize_dir_entries(data, data_len, dir, pool);
}

svn_error_t *
svn_fs_x__replace_dir_entry(void **data,
                            apr_size_t *data_len,
                            void *baton,
                            apr_pool_t *pool)
{
  replace_baton_t *replace_baton = (replace_baton_t *)baton;
  dir_data_t *dir_data = (dir_data_t *)*data;
  svn_boolean_t found;
  svn_fs_x__dirent_t **entries;
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
  entries = (svn_fs_x__dirent_t **)
    svn_temp_deserializer__ptr((const char *)dir_data,
                               (const void *const *)&dir_data->entries);

  /* resolve the reference to the lengths array */
  lengths = (apr_uint32_t *)
    svn_temp_deserializer__ptr((const char *)dir_data,
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
    svn_temp_deserializer__ptr((const char *)dir_data,
                               (const void *const *)&dir_data->lengths);
  lengths[pos] = length;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__reset_txn_filesize(void **data,
                             apr_size_t *data_len,
                             void *baton,
                             apr_pool_t *pool)
{
  dir_data_t *dir_data = (dir_data_t *)*data;
  dir_data->txn_filesize = SVN_INVALID_FILESIZE;

  return SVN_NO_ERROR;
}

svn_error_t  *
svn_fs_x__serialize_rep_header(void **data,
                               apr_size_t *data_len,
                               void *in,
                               apr_pool_t *pool)
{
  *data_len = sizeof(svn_fs_x__rep_header_t);
  *data = in;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__deserialize_rep_header(void **out,
                                 void *data,
                                 apr_size_t data_len,
                                 apr_pool_t *result_pool)
{
  *out = data;

  return SVN_NO_ERROR;
}

/* Utility function to serialize change CHANGE_P in the given serialization
 * CONTEXT.
 */
static void
serialize_change(svn_temp_serializer__context_t *context,
                 svn_fs_x__change_t * const *change_p)
{
  const svn_fs_x__change_t * change = *change_p;
  if (change == NULL)
    return;

  /* serialize the change struct itself */
  svn_temp_serializer__push(context,
                            (const void * const *)change_p,
                            sizeof(*change));

  /* serialize sub-structures */
  svn_temp_serializer__add_string(context, &change->path.data);
  svn_temp_serializer__add_string(context, &change->copyfrom_path);

  /* return to the caller's nesting level */
  svn_temp_serializer__pop(context);
}

/* Utility function to serialize the CHANGE_P within the given
 * serialization CONTEXT.
 */
static void
deserialize_change(void *buffer,
                   svn_fs_x__change_t **change_p)
{
  svn_fs_x__change_t * change;

  /* fix-up of the pointer to the struct in question */
  svn_temp_deserializer__resolve(buffer, (void **)change_p);

  change = *change_p;
  if (change == NULL)
    return;

  /* fix-up of sub-structures */
  svn_temp_deserializer__resolve(change, (void **)&change->path.data);
  svn_temp_deserializer__resolve(change, (void **)&change->copyfrom_path);
}

svn_error_t *
svn_fs_x__serialize_changes(void **data,
                            apr_size_t *data_len,
                            void *in,
                            apr_pool_t *pool)
{
  svn_fs_x__changes_list_t *changes = in;
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
svn_fs_x__deserialize_changes(void **out,
                              void *data,
                              apr_size_t data_len,
                              apr_pool_t *result_pool)
{
  int i;
  svn_fs_x__changes_list_t *changes = (svn_fs_x__changes_list_t *)data;

  /* de-serialize our auxiliary data structure */
  svn_temp_deserializer__resolve(changes, (void**)&changes->changes);

  /* de-serialize each entry and add it to the array */
  for (i = 0; i < changes->count; ++i)
    deserialize_change(changes->changes,
                       (svn_fs_x__change_t **)&changes->changes[i]);

  /* done */
  *out = changes;

  return SVN_NO_ERROR;
}
