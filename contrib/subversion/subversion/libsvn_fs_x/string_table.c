/* string_table.c : operations on string tables
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

#include <assert.h>
#include <string.h>
#include <apr_tables.h>

#include "svn_string.h"
#include "svn_sorts.h"
#include "private/svn_dep_compat.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_packed_data.h"
#include "string_table.h"



#define MAX_DATA_SIZE 0xffff
#define MAX_SHORT_STRING_LEN (MAX_DATA_SIZE / 4)
#define TABLE_SHIFT 13
#define MAX_STRINGS_PER_TABLE (1 << (TABLE_SHIFT - 1))
#define LONG_STRING_MASK (1 << (TABLE_SHIFT - 1))
#define STRING_INDEX_MASK ((1 << (TABLE_SHIFT - 1)) - 1)
#define PADDING (sizeof(apr_uint64_t))


typedef struct builder_string_t
{
  svn_string_t string;
  int position;
  apr_size_t depth;
  struct builder_string_t *previous;
  struct builder_string_t *next;
  apr_size_t previous_match_len;
  apr_size_t next_match_len;
  struct builder_string_t *left;
  struct builder_string_t *right;
} builder_string_t;

typedef struct builder_table_t
{
  apr_size_t max_data_size;
  builder_string_t *top;
  builder_string_t *first;
  builder_string_t *last;
  apr_array_header_t *short_strings;
  apr_array_header_t *long_strings;
  apr_hash_t *long_string_dict;
  apr_size_t long_string_size;
} builder_table_t;

struct string_table_builder_t
{
  apr_pool_t *pool;
  apr_array_header_t *tables;
};

typedef struct string_header_t
{
  apr_uint16_t head_string;
  apr_uint16_t head_length;
  apr_uint16_t tail_start;
  apr_uint16_t tail_length;
} string_header_t;

typedef struct string_sub_table_t
{
  const char *data;
  apr_size_t data_size;

  string_header_t *short_strings;
  apr_size_t short_string_count;

  svn_string_t *long_strings;
  apr_size_t long_string_count;
} string_sub_table_t;

struct string_table_t
{
  apr_size_t size;
  string_sub_table_t *sub_tables;
};


/* Accessing ID Pieces.  */

static builder_table_t *
add_table(string_table_builder_t *builder)
{
  builder_table_t *table = apr_pcalloc(builder->pool, sizeof(*table));
  table->max_data_size = MAX_DATA_SIZE - PADDING; /* ensure there remain a few
                                                     unused bytes at the end */
  table->short_strings = apr_array_make(builder->pool, 64,
                                        sizeof(builder_string_t *));
  table->long_strings = apr_array_make(builder->pool, 0,
                                       sizeof(svn_string_t));
  table->long_string_dict = svn_hash__make(builder->pool);

  APR_ARRAY_PUSH(builder->tables, builder_table_t *) = table;

  return table;
}

string_table_builder_t *
svn_fs_x__string_table_builder_create(apr_pool_t *result_pool)
{
  string_table_builder_t *result = apr_palloc(result_pool, sizeof(*result));
  result->pool = result_pool;
  result->tables = apr_array_make(result_pool, 1, sizeof(builder_table_t *));

  add_table(result);

  return result;
}

static void
balance(builder_table_t *table,
        builder_string_t **parent,
        builder_string_t *node)
{
  apr_size_t left_height = node->left ? node->left->depth + 1 : 0;
  apr_size_t right_height = node->right ? node->right->depth + 1 : 0;

  if (left_height > right_height + 1)
    {
      builder_string_t *temp = node->left->right;
      node->left->right = node;
      *parent = node->left;
      node->left = temp;

      --left_height;
    }
  else if (left_height + 1 < right_height)
    {
      builder_string_t *temp = node->right->left;
      *parent = node->right;
      node->right->left = node;
      node->right = temp;

      --right_height;
    }

  node->depth = MAX(left_height, right_height);
}

static apr_uint16_t
match_length(const svn_string_t *lhs,
             const svn_string_t *rhs)
{
  apr_size_t len = MIN(lhs->len, rhs->len);
  return (apr_uint16_t)svn_cstring__match_length(lhs->data, rhs->data, len);
}

static apr_uint16_t
insert_string(builder_table_t *table,
              builder_string_t **parent,
              builder_string_t *to_insert)
{
  apr_uint16_t result;
  builder_string_t *current = *parent;
  int diff = strcmp(current->string.data, to_insert->string.data);
  if (diff == 0)
    {
      apr_array_pop(table->short_strings);
      return current->position;
    }

  if (diff < 0)
    {
      if (current->left == NULL)
        {
          current->left = to_insert;

          to_insert->previous = current->previous;
          to_insert->next = current;

          if (to_insert->previous == NULL)
            {
              table->first = to_insert;
            }
          else
            {
              builder_string_t *previous = to_insert->previous;
              to_insert->previous_match_len
                = match_length(&previous->string, &to_insert->string);

              previous->next = to_insert;
              previous->next_match_len = to_insert->previous_match_len;
            }

          current->previous = to_insert;
          to_insert->next_match_len
            = match_length(&current->string, &to_insert->string);
          current->previous_match_len = to_insert->next_match_len;

          table->max_data_size -= to_insert->string.len;
          if (to_insert->previous == NULL)
            table->max_data_size += to_insert->next_match_len;
          else
            table->max_data_size += MIN(to_insert->previous_match_len,
                                        to_insert->next_match_len);

          return to_insert->position;
        }
      else
        result = insert_string(table, &current->left, to_insert);
    }
  else
    {
      if (current->right == NULL)
        {
          current->right = to_insert;

          to_insert->next = current->next;
          to_insert->previous = current;

          if (to_insert->next == NULL)
            {
              table->last = to_insert;
            }
          else
            {
              builder_string_t *next = to_insert->next;
              to_insert->next_match_len
                = match_length(&next->string, &to_insert->string);

              next->previous = to_insert;
              next->previous_match_len = to_insert->next_match_len;
            }

          current->next = current->right;
          to_insert->previous_match_len
            = match_length(&current->string, &to_insert->string);
          current->next_match_len = to_insert->previous_match_len;

          table->max_data_size -= to_insert->string.len;
          if (to_insert->next == NULL)
            table->max_data_size += to_insert->previous_match_len;
          else
            table->max_data_size += MIN(to_insert->previous_match_len,
                                        to_insert->next_match_len);

          return to_insert->position;
        }
      else
        result = insert_string(table, &current->right, to_insert);
    }

  balance(table, parent, current);
  return result;
}

apr_size_t
svn_fs_x__string_table_builder_add(string_table_builder_t *builder,
                                   const char *string,
                                   apr_size_t len)
{
  apr_size_t result;
  builder_table_t *table = APR_ARRAY_IDX(builder->tables,
                                         builder->tables->nelts - 1,
                                         builder_table_t *);
  if (len == 0)
    len = strlen(string);

  string = apr_pstrmemdup(builder->pool, string, len);
  if (len > MAX_SHORT_STRING_LEN)
    {
      void *idx_void;
      svn_string_t item;
      item.data = string;
      item.len = len;

      idx_void = apr_hash_get(table->long_string_dict, string, len);
      result = (apr_uintptr_t)idx_void;
      if (result)
        return result - 1
             + LONG_STRING_MASK
             + (((apr_size_t)builder->tables->nelts - 1) << TABLE_SHIFT);

      if (table->long_strings->nelts == MAX_STRINGS_PER_TABLE)
        table = add_table(builder);

      result = table->long_strings->nelts
             + LONG_STRING_MASK
             + (((apr_size_t)builder->tables->nelts - 1) << TABLE_SHIFT);
      APR_ARRAY_PUSH(table->long_strings, svn_string_t) = item;
      apr_hash_set(table->long_string_dict, string, len,
                   (void*)(apr_uintptr_t)table->long_strings->nelts);

      table->long_string_size += len;
    }
  else
    {
      builder_string_t *item = apr_pcalloc(builder->pool, sizeof(*item));
      item->string.data = string;
      item->string.len = len;
      item->previous_match_len = 0;
      item->next_match_len = 0;

      if (   table->short_strings->nelts == MAX_STRINGS_PER_TABLE
          || table->max_data_size < len)
        table = add_table(builder);

      item->position = table->short_strings->nelts;
      APR_ARRAY_PUSH(table->short_strings, builder_string_t *) = item;

      if (table->top == NULL)
        {
          table->max_data_size -= len;
          table->top = item;
          table->first = item;
          table->last = item;

          result = ((apr_size_t)builder->tables->nelts - 1) << TABLE_SHIFT;
        }
      else
        {
          result = insert_string(table, &table->top, item)
                 + (((apr_size_t)builder->tables->nelts - 1) << TABLE_SHIFT);
        }
    }

  return result;
}

apr_size_t
svn_fs_x__string_table_builder_estimate_size(string_table_builder_t *builder)
{
  apr_size_t total = 0;
  int i;

  for (i = 0; i < builder->tables->nelts; ++i)
    {
      builder_table_t *table
        = APR_ARRAY_IDX(builder->tables, i, builder_table_t*);

      /* total number of chars to store,
       * 8 bytes per short string table entry
       * 4 bytes per long string table entry
       * some static overhead */
      apr_size_t table_size
        = MAX_DATA_SIZE - table->max_data_size
        + table->long_string_size
        + table->short_strings->nelts * 8
        + table->long_strings->nelts * 4
        + 10;

      total += table_size;
    }

  /* ZIP compression should give us a 50% reduction.
   * add some static overhead */
  return 200 + total / 2;

}

static void
create_table(string_sub_table_t *target,
             builder_table_t *source,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  int i = 0;
  apr_hash_t *tails = svn_hash__make(scratch_pool);
  svn_stringbuf_t *data
    = svn_stringbuf_create_ensure(MAX_DATA_SIZE - source->max_data_size,
                                  scratch_pool);

  /* pack sub-strings */
  target->short_string_count = (apr_size_t)source->short_strings->nelts;
  target->short_strings = apr_palloc(result_pool,
                                     sizeof(*target->short_strings) *
                                           target->short_string_count);
  for (i = 0; i < source->short_strings->nelts; ++i)
    {
      const builder_string_t *string
        = APR_ARRAY_IDX(source->short_strings, i, const builder_string_t *);

      string_header_t *entry = &target->short_strings[i];
      const char *tail = string->string.data + string->previous_match_len;
      string_header_t *tail_match;
      apr_size_t head_length = string->previous_match_len;

      /* Minimize the number of strings to visit when reconstructing the
         string head.  So, skip all predecessors that don't contribute to
         first HEAD_LENGTH chars of our string. */
      if (head_length)
        {
          const builder_string_t *furthest_prev = string->previous;
          while (furthest_prev->previous_match_len >= head_length)
            furthest_prev = furthest_prev->previous;
          entry->head_string = furthest_prev->position;
        }
      else
        entry->head_string = 0;

      /* head & tail length are known */
      entry->head_length = (apr_uint16_t)head_length;
      entry->tail_length
        = (apr_uint16_t)(string->string.len - entry->head_length);

      /* try to reuse an existing tail segment */
      tail_match = apr_hash_get(tails, tail, entry->tail_length);
      if (tail_match)
        {
          entry->tail_start = tail_match->tail_start;
        }
      else
        {
          entry->tail_start = (apr_uint16_t)data->len;
          svn_stringbuf_appendbytes(data, tail, entry->tail_length);
          apr_hash_set(tails, tail, entry->tail_length, entry);
        }
    }

  /* pack long strings */
  target->long_string_count = (apr_size_t)source->long_strings->nelts;
  target->long_strings = apr_palloc(result_pool,
                                    sizeof(*target->long_strings) *
                                          target->long_string_count);
  for (i = 0; i < source->long_strings->nelts; ++i)
    {
      svn_string_t *string = &target->long_strings[i];
      *string = APR_ARRAY_IDX(source->long_strings, i, svn_string_t);
      string->data = apr_pstrmemdup(result_pool, string->data, string->len);
    }

  data->len += PADDING; /* add a few extra bytes at the end of the buffer
                           that we want to keep valid for chunky access */
  assert(data->len < data->blocksize);
  memset(data->data + data->len - PADDING, 0, PADDING);

  target->data = apr_pmemdup(result_pool, data->data, data->len);
  target->data_size = data->len;
}

string_table_t *
svn_fs_x__string_table_create(const string_table_builder_t *builder,
                              apr_pool_t *result_pool)
{
  apr_size_t i;

  string_table_t *result = apr_pcalloc(result_pool, sizeof(*result));
  result->size = (apr_size_t)builder->tables->nelts;
  result->sub_tables
    = apr_pcalloc(result_pool, result->size * sizeof(*result->sub_tables));

  for (i = 0; i < result->size; ++i)
    create_table(&result->sub_tables[i],
                 APR_ARRAY_IDX(builder->tables, i, builder_table_t*),
                 result_pool,
                 builder->pool);

  return result;
}

/* Masks used by table_copy_string.  copy_mask[I] is used if the target
   content to be preserved starts at byte I within the current chunk.
   This is used to work around alignment issues.
 */
#if SVN_UNALIGNED_ACCESS_IS_OK
static const char *copy_masks[8] = { "\xff\xff\xff\xff\xff\xff\xff\xff",
                                     "\x00\xff\xff\xff\xff\xff\xff\xff",
                                     "\x00\x00\xff\xff\xff\xff\xff\xff",
                                     "\x00\x00\x00\xff\xff\xff\xff\xff",
                                     "\x00\x00\x00\x00\xff\xff\xff\xff",
                                     "\x00\x00\x00\x00\x00\xff\xff\xff",
                                     "\x00\x00\x00\x00\x00\x00\xff\xff",
                                     "\x00\x00\x00\x00\x00\x00\x00\xff" };
#endif

static void
table_copy_string(char *buffer,
                  apr_size_t len,
                  const string_sub_table_t *table,
                  string_header_t *header)
{
  buffer[len] = '\0';
  do
    {
      assert(header->head_length <= len);
        {
#if SVN_UNALIGNED_ACCESS_IS_OK
          /* the sections that we copy tend to be short but we can copy
             *all* of it chunky because we made sure that source and target
             buffer have some extra padding to prevent segfaults. */
          apr_uint64_t mask;
          apr_size_t to_copy = len - header->head_length;
          apr_size_t copied = 0;

          const char *source = table->data + header->tail_start;
          char *target = buffer + header->head_length;
          len = header->head_length;

          /* copy whole chunks */
          while (to_copy >= copied + sizeof(apr_uint64_t))
            {
              *(apr_uint64_t *)(target + copied)
                = *(const apr_uint64_t *)(source + copied);
              copied += sizeof(apr_uint64_t);
            }

          /* copy the remainder assuming that we have up to 8 extra bytes
             of addressable buffer on the source and target sides.
             Now, we simply copy 8 bytes and use a mask to filter & merge
             old with new data. */
          mask = *(const apr_uint64_t *)copy_masks[to_copy - copied];
          *(apr_uint64_t *)(target + copied)
            = (*(apr_uint64_t *)(target + copied) & mask)
            | (*(const apr_uint64_t *)(source + copied) & ~mask);
#else
          memcpy(buffer + header->head_length,
                 table->data + header->tail_start,
                 len - header->head_length);
          len = header->head_length;
#endif
        }

      header = &table->short_strings[header->head_string];
    }
  while (len);
}

const char*
svn_fs_x__string_table_get(const string_table_t *table,
                           apr_size_t idx,
                           apr_size_t *length,
                           apr_pool_t *result_pool)
{
  apr_size_t table_number = idx >> TABLE_SHIFT;
  apr_size_t sub_index = idx & STRING_INDEX_MASK;

  if (table_number < table->size)
    {
      string_sub_table_t *sub_table = &table->sub_tables[table_number];
      if (idx & LONG_STRING_MASK)
        {
          if (sub_index < sub_table->long_string_count)
            {
              if (length)
                *length = sub_table->long_strings[sub_index].len;

              return apr_pstrmemdup(result_pool,
                                    sub_table->long_strings[sub_index].data,
                                    sub_table->long_strings[sub_index].len);
            }
        }
      else
        {
          if (sub_index < sub_table->short_string_count)
            {
              string_header_t *header = sub_table->short_strings + sub_index;
              apr_size_t len = header->head_length + header->tail_length;
              char *result = apr_palloc(result_pool, len + PADDING);

              if (length)
                *length = len;
              table_copy_string(result, len, sub_table, header);

              return result;
            }
        }
    }

  return apr_pstrmemdup(result_pool, "", 0);
}

svn_error_t *
svn_fs_x__write_string_table(svn_stream_t *stream,
                             const string_table_t *table,
                             apr_pool_t *scratch_pool)
{
  apr_size_t i, k;

  svn_packed__data_root_t *root = svn_packed__data_create_root(scratch_pool);

  svn_packed__int_stream_t *table_sizes
    = svn_packed__create_int_stream(root, FALSE, FALSE);
  svn_packed__int_stream_t *small_strings_headers
    = svn_packed__create_int_stream(root, FALSE, FALSE);
  svn_packed__byte_stream_t *large_strings
    = svn_packed__create_bytes_stream(root);
  svn_packed__byte_stream_t *small_strings_data
    = svn_packed__create_bytes_stream(root);

  svn_packed__create_int_substream(small_strings_headers, TRUE, FALSE);
  svn_packed__create_int_substream(small_strings_headers, FALSE, FALSE);
  svn_packed__create_int_substream(small_strings_headers, TRUE, FALSE);
  svn_packed__create_int_substream(small_strings_headers, FALSE, FALSE);

  /* number of sub-tables */

  svn_packed__add_uint(table_sizes, table->size);

  /* all short-string char data sizes */

  for (i = 0; i < table->size; ++i)
    svn_packed__add_uint(table_sizes,
                         table->sub_tables[i].short_string_count);

  for (i = 0; i < table->size; ++i)
    svn_packed__add_uint(table_sizes,
                         table->sub_tables[i].long_string_count);

  /* all strings */

  for (i = 0; i < table->size; ++i)
    {
      string_sub_table_t *sub_table = &table->sub_tables[i];
      svn_packed__add_bytes(small_strings_data,
                            sub_table->data,
                            sub_table->data_size);

      for (k = 0; k < sub_table->short_string_count; ++k)
        {
          string_header_t *string = &sub_table->short_strings[k];

          svn_packed__add_uint(small_strings_headers, string->head_string);
          svn_packed__add_uint(small_strings_headers, string->head_length);
          svn_packed__add_uint(small_strings_headers, string->tail_start);
          svn_packed__add_uint(small_strings_headers, string->tail_length);
        }

      for (k = 0; k < sub_table->long_string_count; ++k)
        svn_packed__add_bytes(large_strings,
                              sub_table->long_strings[k].data,
                              sub_table->long_strings[k].len + 1);
    }

  /* write to target stream */

  SVN_ERR(svn_packed__data_write(stream, root, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__read_string_table(string_table_t **table_p,
                            svn_stream_t *stream,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  apr_size_t i, k;

  string_table_t *table = apr_palloc(result_pool, sizeof(*table));

  svn_packed__data_root_t *root;
  svn_packed__int_stream_t *table_sizes;
  svn_packed__byte_stream_t *large_strings;
  svn_packed__byte_stream_t *small_strings_data;
  svn_packed__int_stream_t *headers;

  SVN_ERR(svn_packed__data_read(&root, stream, result_pool, scratch_pool));
  table_sizes = svn_packed__first_int_stream(root);
  headers = svn_packed__next_int_stream(table_sizes);
  large_strings = svn_packed__first_byte_stream(root);
  small_strings_data = svn_packed__next_byte_stream(large_strings);

  /* create sub-tables */

  table->size = (apr_size_t)svn_packed__get_uint(table_sizes);
  table->sub_tables = apr_pcalloc(result_pool,
                                  table->size * sizeof(*table->sub_tables));

  /* read short strings */

  for (i = 0; i < table->size; ++i)
    {
      string_sub_table_t *sub_table = &table->sub_tables[i];

      sub_table->short_string_count
        = (apr_size_t)svn_packed__get_uint(table_sizes);
      if (sub_table->short_string_count)
        {
          sub_table->short_strings
            = apr_pcalloc(result_pool, sub_table->short_string_count
                                    * sizeof(*sub_table->short_strings));

          /* read short string headers */

          for (k = 0; k < sub_table->short_string_count; ++k)
            {
              string_header_t *string = &sub_table->short_strings[k];

              string->head_string = (apr_uint16_t)svn_packed__get_uint(headers);
              string->head_length = (apr_uint16_t)svn_packed__get_uint(headers);
              string->tail_start = (apr_uint16_t)svn_packed__get_uint(headers);
              string->tail_length = (apr_uint16_t)svn_packed__get_uint(headers);
            }
        }

      sub_table->data = svn_packed__get_bytes(small_strings_data,
                                              &sub_table->data_size);
    }

  /* read long strings */

  for (i = 0; i < table->size; ++i)
    {
      /* initialize long string table */
      string_sub_table_t *sub_table = &table->sub_tables[i];

      sub_table->long_string_count = svn_packed__get_uint(table_sizes);
      if (sub_table->long_string_count)
        {
          sub_table->long_strings
            = apr_pcalloc(result_pool, sub_table->long_string_count
                                    * sizeof(*sub_table->long_strings));

          /* read long strings */

          for (k = 0; k < sub_table->long_string_count; ++k)
            {
              svn_string_t *string = &sub_table->long_strings[k];
              string->data = svn_packed__get_bytes(large_strings,
                                                   &string->len);
              string->len--;
            }
        }
    }

  /* done */

  *table_p = table;

  return SVN_NO_ERROR;
}

void
svn_fs_x__serialize_string_table(svn_temp_serializer__context_t *context,
                                 string_table_t **st)
{
  apr_size_t i, k;
  string_table_t *string_table = *st;
  if (string_table == NULL)
    return;

  /* string table struct */
  svn_temp_serializer__push(context,
                            (const void * const *)st,
                            sizeof(*string_table));

  /* sub-table array (all structs in a single memory block) */
  svn_temp_serializer__push(context,
                            (const void * const *)&string_table->sub_tables,
                            sizeof(*string_table->sub_tables) *
                            string_table->size);

  /* sub-elements of all sub-tables */
  for (i = 0; i < string_table->size; ++i)
    {
      string_sub_table_t *sub_table = &string_table->sub_tables[i];
      svn_temp_serializer__add_leaf(context,
                                    (const void * const *)&sub_table->data,
                                    sub_table->data_size);
      svn_temp_serializer__add_leaf(context,
                    (const void * const *)&sub_table->short_strings,
                    sub_table->short_string_count * sizeof(string_header_t));

      /* all "long string" instances form a single memory block */
      svn_temp_serializer__push(context,
                    (const void * const *)&sub_table->long_strings,
                    sub_table->long_string_count * sizeof(svn_string_t));

      /* serialize actual long string contents */
      for (k = 0; k < sub_table->long_string_count; ++k)
        {
          svn_string_t *string = &sub_table->long_strings[k];
          svn_temp_serializer__add_leaf(context,
                                        (const void * const *)&string->data,
                                        string->len + 1);
        }

      svn_temp_serializer__pop(context);
    }

  /* back to the caller's nesting level */
  svn_temp_serializer__pop(context);
  svn_temp_serializer__pop(context);
}

void
svn_fs_x__deserialize_string_table(void *buffer,
                                   string_table_t **table)
{
  apr_size_t i, k;
  string_sub_table_t *sub_tables;

  svn_temp_deserializer__resolve(buffer, (void **)table);
  if (*table == NULL)
    return;

  svn_temp_deserializer__resolve(*table, (void **)&(*table)->sub_tables);
  sub_tables = (*table)->sub_tables;
  for (i = 0; i < (*table)->size; ++i)
    {
      string_sub_table_t *sub_table = sub_tables + i;

      svn_temp_deserializer__resolve(sub_tables,
                                     (void **)&sub_table->data);
      svn_temp_deserializer__resolve(sub_tables,
                                     (void **)&sub_table->short_strings);
      svn_temp_deserializer__resolve(sub_tables,
                                     (void **)&sub_table->long_strings);

      for (k = 0; k < sub_table->long_string_count; ++k)
        svn_temp_deserializer__resolve(sub_table->long_strings,
                               (void **)&sub_table->long_strings[k].data);
    }
}

const char*
svn_fs_x__string_table_get_func(const string_table_t *table,
                                apr_size_t idx,
                                apr_size_t *length,
                                apr_pool_t *result_pool)
{
  apr_size_t table_number = idx >> TABLE_SHIFT;
  apr_size_t sub_index = idx & STRING_INDEX_MASK;

  if (table_number < table->size)
    {
      /* resolve TABLE->SUB_TABLES pointer and select sub-table */
      string_sub_table_t *sub_tables
        = (string_sub_table_t *)svn_temp_deserializer__ptr(table,
                                   (const void *const *)&table->sub_tables);
      string_sub_table_t *sub_table = sub_tables + table_number;

      /* pick the right kind of string */
      if (idx & LONG_STRING_MASK)
        {
          if (sub_index < sub_table->long_string_count)
            {
              /* resolve SUB_TABLE->LONG_STRINGS, select the string we want
                 and resolve the pointer to its char data */
              svn_string_t *long_strings
                = (svn_string_t *)svn_temp_deserializer__ptr(sub_table,
                             (const void *const *)&sub_table->long_strings);
              const char *str_data
                = (const char*)svn_temp_deserializer__ptr(long_strings,
                        (const void *const *)&long_strings[sub_index].data);

              /* return a copy of the char data */
              if (length)
                *length = long_strings[sub_index].len;

              return apr_pstrmemdup(result_pool,
                                    str_data,
                                    long_strings[sub_index].len);
            }
        }
      else
        {
          if (sub_index < sub_table->short_string_count)
            {
              string_header_t *header;
              apr_size_t len;
              char *result;

              /* construct a copy of our sub-table struct with SHORT_STRINGS
                 and DATA pointers resolved.  Leave all other pointers as
                 they are.  This allows us to use the same code for string
                 reconstruction here as in the non-serialized case. */
              string_sub_table_t table_copy = *sub_table;
              table_copy.data
                = (const char *)svn_temp_deserializer__ptr(sub_tables,
                                     (const void *const *)&sub_table->data);
              table_copy.short_strings
                = (string_header_t *)svn_temp_deserializer__ptr(sub_tables,
                            (const void *const *)&sub_table->short_strings);

              /* reconstruct the char data and return it */
              header = table_copy.short_strings + sub_index;
              len = header->head_length + header->tail_length;
              result = apr_palloc(result_pool, len + PADDING);
              if (length)
                *length = len;

              table_copy_string(result, len, &table_copy, header);

              return result;
            }
        }
    }

  return "";
}
