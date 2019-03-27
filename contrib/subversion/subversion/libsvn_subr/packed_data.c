/* packed_data.c : implement the packed binary stream data structure
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

#include <apr_tables.h>

#include "svn_string.h"
#include "svn_sorts.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_delta_private.h"
#include "private/svn_packed_data.h"

#include "svn_private_config.h"



/* Private int stream data referenced by svn_packed__int_stream_t.
 */
typedef struct packed_int_private_t
{
  /* First sub-stream, if any.  NULL otherwise. */
  svn_packed__int_stream_t *first_substream;

  /* Last sub-stream, if any.  NULL otherwise. */
  svn_packed__int_stream_t *last_substream;

  /* Current sub-stream to read from / write to, if any.  NULL otherwise.
     This will be initialized to FIRST_SUBSTREAM and then advanced in a
     round-robin scheme after each number being read. */
  svn_packed__int_stream_t *current_substream;

  /* Number of sub-streams. */
  apr_size_t substream_count;

  /* Next (sibling) integer stream.  If this is the last one, points to
     the first in the list (i.e. this forms a ring list).  Never NULL. */
  svn_packed__int_stream_t *next;

  /* 7b/8b encoded integer values (previously diff'ed and sign-handled,
     if indicated by the flags below).  The contents are disjoint from
     the unparsed number buffer.  May be NULL while not written to. */
  svn_stringbuf_t *packed;

  /* Initialized to 0.  Latest value written to / read from PACKED.
     Undefined if DIFF is FALSE. */
  apr_uint64_t last_value;

  /* Deltify data before storing it in PACKED. */
  svn_boolean_t diff;

  /* Numbers are likely to contain negative values with small absolutes.
     If TRUE, store the signed bit in LSB before encoding. */
  svn_boolean_t is_signed;

  /* Number of integers in this stream. */
  apr_size_t item_count;

  /* TRUE for the last stream in a list of siblings. */
  svn_boolean_t is_last;

  /* Pool to use for allocations. */
  apr_pool_t *pool;
} packed_int_private_t;

/* A byte sequence stream.  Please note that NEXT is defined different
 * from the NEXT member in integer streams.
 */
struct svn_packed__byte_stream_t
{
  /* First sub-stream, if any.  NULL otherwise. */
  svn_packed__byte_stream_t *first_substream;

  /* Last sub-stream, if any.  NULL otherwise. */
  svn_packed__byte_stream_t *last_substream;

  /* Next (sibling) byte sequence stream, if any.  NULL otherwise. */
  svn_packed__byte_stream_t *next;

  /* Stream to store the sequence lengths. */
  svn_packed__int_stream_t *lengths_stream;

  /* It's index (relative to its parent). */
  apr_size_t lengths_stream_index;

  /* Concatenated byte sequences. */
  svn_stringbuf_t *packed;

  /* Pool to use for allocations. */
  apr_pool_t *pool;
};

/* The serialization root object.  It references the top-level streams.
 */
struct svn_packed__data_root_t
{
  /* First top-level integer stream, if any.  NULL otherwise. */
  svn_packed__int_stream_t *first_int_stream;

  /* Last top-level integer stream, if any.  NULL otherwise. */
  svn_packed__int_stream_t *last_int_stream;

  /* Number of top-level integer streams. */
  apr_size_t int_stream_count;

  /* First top-level byte sequence stream, if any.  NULL otherwise. */
  svn_packed__byte_stream_t *first_byte_stream;

  /* Last top-level byte sequence stream, if any.  NULL otherwise. */
  svn_packed__byte_stream_t *last_byte_stream;

  /* Number of top-level byte sequence streams. */
  apr_size_t byte_stream_count;

  /* Pool to use for allocations. */
  apr_pool_t *pool;
};

/* Write access. */

svn_packed__data_root_t *
svn_packed__data_create_root(apr_pool_t *pool)
{
  svn_packed__data_root_t *root = apr_pcalloc(pool, sizeof(*root));
  root->pool = pool;

  return root;
}

svn_packed__int_stream_t *
svn_packed__create_int_stream(svn_packed__data_root_t *root,
                              svn_boolean_t diff,
                              svn_boolean_t signed_ints)
{
  /* allocate and initialize the stream node */
  packed_int_private_t *private_data
    = apr_pcalloc(root->pool, sizeof(*private_data));
  svn_packed__int_stream_t *stream
    = apr_palloc(root->pool, sizeof(*stream));

  private_data->diff = diff;
  private_data->is_signed = signed_ints;
  private_data->is_last = TRUE;
  private_data->pool = root->pool;

  stream->buffer_used = 0;
  stream->private_data = private_data;

  /* maintain the ring list */
  if (root->last_int_stream)
    {
      packed_int_private_t *previous_private_data
        = root->last_int_stream->private_data;
      previous_private_data->next = stream;
      previous_private_data->is_last = FALSE;
    }
  else
    {
      root->first_int_stream = stream;
    }

  root->last_int_stream = stream;
  root->int_stream_count++;

  return stream;
}

svn_packed__int_stream_t *
svn_packed__create_int_substream(svn_packed__int_stream_t *parent,
                                 svn_boolean_t diff,
                                 svn_boolean_t signed_ints)
{
  packed_int_private_t *parent_private = parent->private_data;

  /* allocate and initialize the stream node */
  packed_int_private_t *private_data
    = apr_pcalloc(parent_private->pool, sizeof(*private_data));
  svn_packed__int_stream_t *stream
    = apr_palloc(parent_private->pool, sizeof(*stream));

  private_data->diff = diff;
  private_data->is_signed = signed_ints;
  private_data->is_last = TRUE;
  private_data->pool = parent_private->pool;

  stream->buffer_used = 0;
  stream->private_data = private_data;

  /* maintain the ring list */
  if (parent_private->last_substream)
    {
      packed_int_private_t *previous_private_data
        = parent_private->last_substream->private_data;
      previous_private_data->next = stream;
      previous_private_data->is_last = FALSE;
    }
  else
    {
      parent_private->first_substream = stream;
      parent_private->current_substream = stream;
    }

  parent_private->last_substream = stream;
  parent_private->substream_count++;
  private_data->next = parent_private->first_substream;

  return stream;
}

/* Returns a new top-level byte sequence stream for ROOT but does not
 * initialize the LENGTH_STREAM member.
 */
static svn_packed__byte_stream_t *
create_bytes_stream_body(svn_packed__data_root_t *root)
{
  svn_packed__byte_stream_t *stream
    = apr_pcalloc(root->pool, sizeof(*stream));

  stream->packed = svn_stringbuf_create_empty(root->pool);

  if (root->last_byte_stream)
    root->last_byte_stream->next = stream;
  else
    root->first_byte_stream = stream;

  root->last_byte_stream = stream;
  root->byte_stream_count++;

  return stream;
}

svn_packed__byte_stream_t *
svn_packed__create_bytes_stream(svn_packed__data_root_t *root)
{
  svn_packed__byte_stream_t *stream
    = create_bytes_stream_body(root);

  stream->lengths_stream_index = root->int_stream_count;
  stream->lengths_stream = svn_packed__create_int_stream(root, FALSE, FALSE);

  return stream;
}

/* Write the 7b/8b representation of VALUE into BUFFER.  BUFFER must
 * provide at least 10 bytes space.
 * Returns the first position behind the written data.
 */
static unsigned char *
write_packed_uint_body(unsigned char *buffer, apr_uint64_t value)
{
  while (value >= 0x80)
    {
      *(buffer++) = (unsigned char)((value % 0x80) + 0x80);
      value /= 0x80;
    }

  *(buffer++) = (unsigned char)value;
  return buffer;
}

/* Return remapped VALUE.
 *
 * Due to sign conversion and diff underflow, values close to UINT64_MAX
 * are almost as frequent as those close to 0.  Remap them such that the
 * MSB is stored in the LSB and the remainder stores the absolute distance
 * to 0.
 *
 * This minimizes the absolute value to store in many scenarios.
 * Hence, the variable-length representation on disk is shorter, too.
 */
static apr_uint64_t
remap_uint(apr_uint64_t value)
{
  return value & APR_UINT64_C(0x8000000000000000)
       ? APR_UINT64_MAX - (2 * value)
       : 2 * value;
}

/* Invert remap_uint. */
static apr_uint64_t
unmap_uint(apr_uint64_t value)
{
  return value & 1
       ? (APR_UINT64_MAX - value / 2)
       : value / 2;
}

/* Empty the unprocessed integer buffer in STREAM by either pushing the
 * data to the sub-streams or writing to the packed data (in case there
 * are no sub-streams).
 */
static void
data_flush_buffer(svn_packed__int_stream_t *stream)
{
  packed_int_private_t *private_data = stream->private_data;
  apr_size_t i;

  /* if we have sub-streams, push the data down to them */
  if (private_data->current_substream)
    for (i = 0; i < stream->buffer_used; ++i)
      {
        packed_int_private_t *current_private_data
          = private_data->current_substream->private_data;

        svn_packed__add_uint(private_data->current_substream,
                             stream->buffer[i]);
        private_data->current_substream = current_private_data->next;
      }
  else
    {
      /* pack the numbers into our local PACKED buffer */

      /* temporary buffer, max 10 bytes required per 7b/8b encoded number */
      unsigned char local_buffer[10 * SVN__PACKED_DATA_BUFFER_SIZE];
      unsigned char *p = local_buffer;

      /* if configured, deltify numbers before packing them.
         Since delta may be negative, always use the 'signed' encoding. */
      if (private_data->diff)
        {
          apr_uint64_t last_value = private_data->last_value;
          for (i = 0; i < stream->buffer_used; ++i)
            {
              apr_uint64_t temp = stream->buffer[i];
              stream->buffer[i] = remap_uint(temp - last_value);
              last_value = temp;
            }

          private_data->last_value = last_value;
        }

      /* if configured and not already done by the deltification above,
         transform to 'signed' encoding.  Store the sign in the LSB and
         the absolute value (-1 for negative values) in the remaining
         63 bits. */
      if (!private_data->diff && private_data->is_signed)
        for (i = 0; i < stream->buffer_used; ++i)
          stream->buffer[i] = remap_uint(stream->buffer[i]);

      /* auto-create packed data buffer.  Give it some reasonable initial
         size - just enough for a few tens of values. */
      if (private_data->packed == NULL)
        private_data->packed
          = svn_stringbuf_create_ensure(256, private_data->pool);

      /* encode numbers into our temp buffer. */
      for (i = 0; i < stream->buffer_used; ++i)
        p = write_packed_uint_body(p, stream->buffer[i]);

      /* append them to the final packed data */
      svn_stringbuf_appendbytes(private_data->packed,
                                (char *)local_buffer,
                                p - local_buffer);
    }

  /* maintain counters */
  private_data->item_count += stream->buffer_used;
  stream->buffer_used = 0;
}

void
svn_packed__add_uint(svn_packed__int_stream_t *stream,
                     apr_uint64_t value)
{
  stream->buffer[stream->buffer_used] = value;
  if (++stream->buffer_used == SVN__PACKED_DATA_BUFFER_SIZE)
    data_flush_buffer(stream);
}

void
svn_packed__add_int(svn_packed__int_stream_t *stream,
                    apr_int64_t value)
{
  svn_packed__add_uint(stream, (apr_uint64_t)value);
}

void
svn_packed__add_bytes(svn_packed__byte_stream_t *stream,
                      const char *data,
                      apr_size_t len)
{
  svn_packed__add_uint(stream->lengths_stream, len);
  svn_stringbuf_appendbytes(stream->packed, data, len);
}

/* Append the 7b/8b encoded representation of VALUE to PACKED.
 */
static void
write_packed_uint(svn_stringbuf_t* packed, apr_uint64_t value)
{
  if (value < 0x80)
    {
      svn_stringbuf_appendbyte(packed, (char)value);
    }
  else
    {
      unsigned char buffer[10];
      unsigned char *p = write_packed_uint_body(buffer, value);

      svn_stringbuf_appendbytes(packed, (char *)buffer, p - buffer);
    }
}

/* Recursively write the structure (config parameters, sub-streams, data
 * sizes) of the STREAM and all its siblings to the TREE_STRUCT buffer.
 */
static void
write_int_stream_structure(svn_stringbuf_t* tree_struct,
                           svn_packed__int_stream_t* stream)
{
  while (stream)
    {
      /* store config parameters and number of sub-streams in 1 number */
      packed_int_private_t *private_data = stream->private_data;
      write_packed_uint(tree_struct, (private_data->substream_count << 2)
                                   + (private_data->diff ? 1 : 0)
                                   + (private_data->is_signed ? 2 : 0));

      /* store item count and length their of packed representation */
      data_flush_buffer(stream);

      write_packed_uint(tree_struct, private_data->item_count);
      write_packed_uint(tree_struct, private_data->packed
                                   ? private_data->packed->len
                                   : 0);

      /* append all sub-stream structures */
      write_int_stream_structure(tree_struct, private_data->first_substream);

      /* continue with next sibling */
      stream = private_data->is_last ? NULL : private_data->next;
    }
}

/* Recursively write the structure (sub-streams, data sizes) of the STREAM
 * and all its siblings to the TREE_STRUCT buffer.
 */
static void
write_byte_stream_structure(svn_stringbuf_t* tree_struct,
                            svn_packed__byte_stream_t* stream)
{
  /* for this and all siblings */
  for (; stream; stream = stream->next)
    {
      /* this stream's structure and size */
      write_packed_uint(tree_struct, 0);
      write_packed_uint(tree_struct, stream->lengths_stream_index);
      write_packed_uint(tree_struct, stream->packed->len);

      /* followed by all its sub-streams */
      write_byte_stream_structure(tree_struct, stream->first_substream);
    }
}

/* Write the 7b/8b encoded representation of VALUE to STREAM.
 */
static svn_error_t *
write_stream_uint(svn_stream_t *stream,
                  apr_uint64_t value)
{
  unsigned char buffer[10];
  apr_size_t count = write_packed_uint_body(buffer, value) - buffer;

  SVN_ERR(svn_stream_write(stream, (char *)buffer, &count));

  return SVN_NO_ERROR;
}

/* Return the total size of all packed data in STREAM, its siblings and
 * all sub-streams.  To get an accurate value, flush all buffers prior to
 * calling this function.
 */
static apr_size_t
packed_int_stream_length(svn_packed__int_stream_t *stream)
{
  packed_int_private_t *private_data = stream->private_data;
  apr_size_t result = private_data->packed ? private_data->packed->len : 0;

  stream = private_data->first_substream;
  while (stream)
    {
      private_data = stream->private_data;
      result += packed_int_stream_length(stream);
      stream = private_data->is_last ? NULL : private_data->next;
    }

  return result;
}

/* Return the total size of all byte sequences data in STREAM, its siblings
 * and all sub-streams.
 */
static apr_size_t
packed_byte_stream_length(svn_packed__byte_stream_t *stream)
{
  apr_size_t result = stream->packed->len;

  for (stream = stream->first_substream; stream; stream = stream->next)
    result += packed_byte_stream_length(stream);

  return result;
}

/* Append all packed data in STREAM, its siblings and all sub-streams to
 * COMBINED.
 */
static void
append_int_stream(svn_packed__int_stream_t *stream,
                  svn_stringbuf_t *combined)
{
  packed_int_private_t *private_data = stream->private_data;
  if (private_data->packed)
    svn_stringbuf_appendstr(combined, private_data->packed);

  stream = private_data->first_substream;
  while (stream)
    {
      private_data = stream->private_data;
      append_int_stream(stream, combined);
      stream = private_data->is_last ? NULL : private_data->next;
    }
}

/* Append all byte sequences in STREAM, its siblings and all sub-streams
 * to COMBINED.
 */
static void
append_byte_stream(svn_packed__byte_stream_t *stream,
                   svn_stringbuf_t *combined)
{
  svn_stringbuf_appendstr(combined, stream->packed);

  for (stream = stream->first_substream; stream; stream = stream->next)
    append_byte_stream(stream, combined);
}

/* Take the binary data in UNCOMPRESSED, zip it into COMPRESSED and write
 * it to STREAM.  COMPRESSED simply acts as a re-usable memory buffer.
 * Clear all buffers (COMPRESSED, UNCOMPRESSED) at the end of the function.
 */
static svn_error_t *
write_stream_data(svn_stream_t *stream,
                  svn_stringbuf_t *uncompressed,
                  svn_stringbuf_t *compressed)
{
  SVN_ERR(svn__compress_zlib(uncompressed->data, uncompressed->len,
                             compressed,
                             SVN_DELTA_COMPRESSION_LEVEL_DEFAULT));

  SVN_ERR(write_stream_uint(stream, compressed->len));
  SVN_ERR(svn_stream_write(stream, compressed->data, &compressed->len));

  svn_stringbuf_setempty(uncompressed);
  svn_stringbuf_setempty(compressed);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_packed__data_write(svn_stream_t *stream,
                       svn_packed__data_root_t *root,
                       apr_pool_t *scratch_pool)
{
  svn_packed__int_stream_t *int_stream;
  svn_packed__byte_stream_t *byte_stream;

  /* re-usable data buffers */
  svn_stringbuf_t *compressed
    = svn_stringbuf_create_ensure(1024, scratch_pool);
  svn_stringbuf_t *uncompressed
    = svn_stringbuf_create_ensure(1024, scratch_pool);

  /* write tree structure */
  svn_stringbuf_t *tree_struct
    = svn_stringbuf_create_ensure(127, scratch_pool);

  write_packed_uint(tree_struct, root->int_stream_count);
  write_int_stream_structure(tree_struct, root->first_int_stream);

  write_packed_uint(tree_struct, root->byte_stream_count);
  write_byte_stream_structure(tree_struct, root->first_byte_stream);

  SVN_ERR(write_stream_uint(stream, tree_struct->len));
  SVN_ERR(svn_stream_write(stream, tree_struct->data, &tree_struct->len));

  /* flatten sub-streams, zip them and write them to disk */

  for (int_stream = root->first_int_stream;
       int_stream;
       int_stream = ((packed_int_private_t*)int_stream->private_data)->next)
    {
      apr_size_t len = packed_int_stream_length(int_stream);
      svn_stringbuf_ensure(uncompressed, len);

      append_int_stream(int_stream, uncompressed);
      SVN_ERR(write_stream_data(stream, uncompressed, compressed));
    }

  for (byte_stream = root->first_byte_stream;
       byte_stream;
       byte_stream = byte_stream->next)
    {
      apr_size_t len = packed_byte_stream_length(byte_stream);
      svn_stringbuf_ensure(uncompressed, len);

      append_byte_stream(byte_stream, uncompressed);
      SVN_ERR(write_stream_data(stream, uncompressed, compressed));
    }

  return SVN_NO_ERROR;
}


/* Read access. */

svn_packed__int_stream_t *
svn_packed__first_int_stream(svn_packed__data_root_t *root)
{
  return root->first_int_stream;
}

svn_packed__byte_stream_t *
svn_packed__first_byte_stream(svn_packed__data_root_t *root)
{
  return root->first_byte_stream;
}

svn_packed__int_stream_t *
svn_packed__next_int_stream(svn_packed__int_stream_t *stream)
{
  packed_int_private_t *private_data = stream->private_data;
  return private_data->is_last ? NULL : private_data->next;
}

svn_packed__byte_stream_t *
svn_packed__next_byte_stream(svn_packed__byte_stream_t *stream)
{
  return stream->next;
}

svn_packed__int_stream_t *
svn_packed__first_int_substream(svn_packed__int_stream_t *stream)
{
  packed_int_private_t *private_data = stream->private_data;
  return private_data->first_substream;
}

apr_size_t
svn_packed__int_count(svn_packed__int_stream_t *stream)
{
  packed_int_private_t *private_data = stream->private_data;
  return private_data->item_count + stream->buffer_used;
}

apr_size_t
svn_packed__byte_count(svn_packed__byte_stream_t *stream)
{
  return stream->packed->len;
}

apr_size_t
svn_packed__byte_block_count(svn_packed__byte_stream_t *stream)
{
  return svn_packed__int_count(stream->lengths_stream);
}

/* Read one 7b/8b encoded value from *P and return it in *RESULT.  Returns
 * the first position after the parsed data.
 *
 * Overflows will be detected in the sense that it will end parsing the
 * input but the result is undefined.
 */
static unsigned char *
read_packed_uint_body(unsigned char *p, apr_uint64_t *result)
{
  if (*p < 0x80)
    {
      *result = *p;
    }
  else
    {
      apr_uint64_t shift = 0;
      apr_uint64_t value = 0;
      while (*p >= 0x80)
        {
          value += (apr_uint64_t)(*p & 0x7f) << shift;
          ++p;

          shift += 7;
          if (shift > 64)
            {
              /* a definite overflow.  Note, that numbers of 65 .. 70
                 bits will not be detected as an overflow as they don't
                 threaten to exceed the input buffer. */
              *result = 0;
              return p;
            }
        }

      *result = value + ((apr_uint64_t)*p << shift);
    }

  return ++p;
}

/* Read one 7b/8b encoded value from STREAM and return it in *RESULT.
 *
 * Overflows will be detected in the sense that it will end parsing the
 * input but the result is undefined.
 */
static svn_error_t *
read_stream_uint(svn_stream_t *stream, apr_uint64_t *result)
{
  apr_uint64_t shift = 0;
  apr_uint64_t value = 0;
  unsigned char c;

  do
    {
      apr_size_t len = 1;
      SVN_ERR(svn_stream_read_full(stream, (char *)&c, &len));
      if (len != 1)
        return svn_error_create(SVN_ERR_CORRUPT_PACKED_DATA, NULL,
                                _("Unexpected end of stream"));

      value += (apr_uint64_t)(c & 0x7f) << shift;
      shift += 7;
      if (shift > 64)
        return svn_error_create(SVN_ERR_CORRUPT_PACKED_DATA, NULL,
                                _("Integer representation too long"));
    }
  while (c >= 0x80);

  *result = value;
  return SVN_NO_ERROR;
}

/* Extract and return the next integer from PACKED and make PACKED point
 * to the next integer.
 */
static apr_uint64_t
read_packed_uint(svn_stringbuf_t *packed)
{
  apr_uint64_t result = 0;
  unsigned char *p = (unsigned char *)packed->data;
  apr_size_t read = read_packed_uint_body(p, &result) - p;

  if (read > packed->len)
    read = packed->len;

  packed->data += read;
  packed->blocksize -= read;
  packed->len -= read;

  return result;
}

/* Ensure that STREAM contains at least one item in its buffer.
 */
static void
svn_packed__data_fill_buffer(svn_packed__int_stream_t *stream)
{
  packed_int_private_t *private_data = stream->private_data;
  apr_size_t i;
  apr_size_t end = MIN(SVN__PACKED_DATA_BUFFER_SIZE,
                       private_data->item_count);

  /* in case, some user calls us explicitly without a good reason ... */
  if (stream->buffer_used)
    return;

  /* can we get data from the sub-streams or do we have to decode it from
     our local packed container? */
  if (private_data->current_substream)
    for (i = end; i > 0; --i)
      {
        packed_int_private_t *current_private_data
          = private_data->current_substream->private_data;
        stream->buffer[i-1]
          = svn_packed__get_uint(private_data->current_substream);
        private_data->current_substream = current_private_data->next;
      }
  else
    {
      /* use this local buffer only if the packed data is shorter than this.
         The goal is that read_packed_uint_body doesn't need check for
         overflows. */
      unsigned char local_buffer[10 * SVN__PACKED_DATA_BUFFER_SIZE];
      unsigned char *p;
      unsigned char *start;
      apr_size_t packed_read;

      if (private_data->packed->len < sizeof(local_buffer))
        {
          apr_size_t trail = sizeof(local_buffer) - private_data->packed->len;
          memcpy(local_buffer,
                 private_data->packed->data,
                 private_data->packed->len);
          memset(local_buffer + private_data->packed->len, 0, MIN(trail, end));

          p = local_buffer;
        }
      else
        p = (unsigned char *)private_data->packed->data;

      /* unpack numbers */
      start = p;
      for (i = end; i > 0; --i)
        p = read_packed_uint_body(p, &stream->buffer[i-1]);

      /* adjust remaining packed data buffer */
      packed_read = p - start;
      private_data->packed->data += packed_read;
      private_data->packed->len -= packed_read;
      private_data->packed->blocksize -= packed_read;

      /* undeltify numbers, if configured */
      if (private_data->diff)
        {
          apr_uint64_t last_value = private_data->last_value;
          for (i = end; i > 0; --i)
            {
              last_value += unmap_uint(stream->buffer[i-1]);
              stream->buffer[i-1] = last_value;
            }

          private_data->last_value = last_value;
        }

      /* handle signed values, if configured and not handled already */
      if (!private_data->diff && private_data->is_signed)
        for (i = 0; i < end; ++i)
          stream->buffer[i] = unmap_uint(stream->buffer[i]);
    }

  stream->buffer_used = end;
  private_data->item_count -= end;
}

apr_uint64_t
svn_packed__get_uint(svn_packed__int_stream_t *stream)
{
  if (stream->buffer_used == 0)
    svn_packed__data_fill_buffer(stream);

  return stream->buffer_used ? stream->buffer[--stream->buffer_used] : 0;
}

apr_int64_t
svn_packed__get_int(svn_packed__int_stream_t *stream)
{
  return (apr_int64_t)svn_packed__get_uint(stream);
}

const char *
svn_packed__get_bytes(svn_packed__byte_stream_t *stream,
                      apr_size_t *len)
{
  const char *result = stream->packed->data;
  apr_size_t count = (apr_size_t)svn_packed__get_uint(stream->lengths_stream);

  if (count > stream->packed->len)
    count = stream->packed->len;

  /* advance packed buffer */
  stream->packed->data += count;
  stream->packed->len -= count;
  stream->packed->blocksize -= count;

  *len = count;
  return result;
}

/* Read the integer stream structure and recreate it in STREAM, including
 * sub-streams, from TREE_STRUCT.
 */
static void
read_int_stream_structure(svn_stringbuf_t *tree_struct,
                          svn_packed__int_stream_t *stream)
{
  packed_int_private_t *private_data = stream->private_data;
  apr_uint64_t value = read_packed_uint(tree_struct);
  apr_size_t substream_count;
  apr_size_t i;

  /* extract local parameters */
  private_data->diff = (value & 1) != 0;
  private_data->is_signed = (value & 2) != 0;
  substream_count = (apr_size_t)(value >> 2);

  /* read item count & packed size; allocate packed data buffer */
  private_data->item_count = (apr_size_t)read_packed_uint(tree_struct);
  value = read_packed_uint(tree_struct);
  if (value)
    {
      private_data->packed = svn_stringbuf_create_ensure((apr_size_t)value,
                                                         private_data->pool);
      private_data->packed->len = (apr_size_t)value;
    }

  /* add sub-streams and read their config, too */
  for (i = 0; i < substream_count; ++i)
    read_int_stream_structure(tree_struct,
                              svn_packed__create_int_substream(stream,
                                                               FALSE,
                                                               FALSE));
}

/* Read the integer stream structure and recreate it in STREAM, including
 * sub-streams, from TREE_STRUCT.  FIRST_INT_STREAM is the integer stream
 * that would correspond to lengths_stream_index 0.
 */
static void
read_byte_stream_structure(svn_stringbuf_t *tree_struct,
                           svn_packed__byte_stream_t *stream,
                           svn_packed__int_stream_t *first_int_stream)
{
  apr_size_t lengths_stream_index;
  apr_size_t packed_size;
  apr_size_t i;

  /* read parameters from the TREE_STRUCT buffer */
  (void) (apr_size_t)read_packed_uint(tree_struct); /* discard first uint */
  lengths_stream_index = (apr_size_t)read_packed_uint(tree_struct);
  packed_size = (apr_size_t)read_packed_uint(tree_struct);

  /* allocate byte sequence buffer size */
  svn_stringbuf_ensure(stream->packed, packed_size);
  stream->packed->len = packed_size;

  /* navigate to the (already existing) lengths_stream */
  stream->lengths_stream_index = lengths_stream_index;
  stream->lengths_stream = first_int_stream;
  for (i = 0; i < lengths_stream_index; ++i)
    {
      packed_int_private_t *length_private
        = stream->lengths_stream->private_data;
      stream->lengths_stream = length_private->next;
    }
}

/* Read a compressed block from STREAM and uncompress it into UNCOMPRESSED.
 * UNCOMPRESSED_LEN is the expected size of the stream.  COMPRESSED is a
 * re-used buffer for temporary data.
 */
static svn_error_t *
read_stream_data(svn_stream_t *stream,
                 apr_size_t uncompressed_len,
                 svn_stringbuf_t *uncompressed,
                 svn_stringbuf_t *compressed)
{
  apr_uint64_t len;
  apr_size_t compressed_len;

  SVN_ERR(read_stream_uint(stream, &len));
  compressed_len = (apr_size_t)len;

  svn_stringbuf_ensure(compressed, compressed_len);
  compressed->len = compressed_len;
  SVN_ERR(svn_stream_read_full(stream, compressed->data, &compressed->len));
  compressed->data[compressed_len] = '\0';

  SVN_ERR(svn__decompress_zlib(compressed->data, compressed->len,
                               uncompressed, uncompressed_len));

  return SVN_NO_ERROR;
}

/* Read the packed contents from COMBINED, starting at *OFFSET and store
 * it in STREAM.  Update *OFFSET to point to the next stream's data and
 * continue with the sub-streams.
 */
static void
unflatten_int_stream(svn_packed__int_stream_t *stream,
                     svn_stringbuf_t *combined,
                     apr_size_t *offset)
{
  packed_int_private_t *private_data = stream->private_data;
  if (private_data->packed)
    {
      memcpy(private_data->packed->data,
             combined->data + *offset,
             private_data->packed->len);

      private_data->packed->data[private_data->packed->len] = '\0';
      *offset += private_data->packed->len;
    }

  stream = private_data->first_substream;
  while (stream)
    {
      private_data = stream->private_data;
      unflatten_int_stream(stream, combined, offset);
      stream = private_data->is_last ? NULL : private_data->next;
    }
}

/* Read the packed contents from COMBINED, starting at *OFFSET and store
 * it in STREAM.  Update *OFFSET to point to the next stream's data and
 * continue with the sub-streams.
 */
static void
unflatten_byte_stream(svn_packed__byte_stream_t *stream,
                      svn_stringbuf_t *combined,
                      apr_size_t *offset)
{
  memcpy(stream->packed->data,
         combined->data + *offset,
         stream->packed->len);
  stream->packed->data[stream->packed->len] = '\0';

  *offset += stream->packed->len;
  for (stream = stream->first_substream; stream; stream = stream->next)
    unflatten_byte_stream(stream, combined, offset);
}

svn_error_t *
svn_packed__data_read(svn_packed__data_root_t **root_p,
                      svn_stream_t *stream,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  apr_uint64_t i;
  apr_uint64_t count;

  svn_packed__int_stream_t *int_stream;
  svn_packed__byte_stream_t *byte_stream;
  svn_packed__data_root_t *root = svn_packed__data_create_root(result_pool);

  svn_stringbuf_t *compressed
    = svn_stringbuf_create_ensure(1024, scratch_pool);
  svn_stringbuf_t *uncompressed
    = svn_stringbuf_create_ensure(1024, scratch_pool);

  /* read tree structure */

  apr_uint64_t tree_struct_size;
  svn_stringbuf_t *tree_struct;

  SVN_ERR(read_stream_uint(stream, &tree_struct_size));
  tree_struct
    = svn_stringbuf_create_ensure((apr_size_t)tree_struct_size, scratch_pool);
  tree_struct->len = (apr_size_t)tree_struct_size;

  SVN_ERR(svn_stream_read_full(stream, tree_struct->data, &tree_struct->len));
  tree_struct->data[tree_struct->len] = '\0';

  /* reconstruct tree structure */

  count = read_packed_uint(tree_struct);
  for (i = 0; i < count; ++i)
    read_int_stream_structure(tree_struct,
                              svn_packed__create_int_stream(root, FALSE,
                                                                 FALSE));

  count = read_packed_uint(tree_struct);
  for (i = 0; i < count; ++i)
    read_byte_stream_structure(tree_struct,
                               create_bytes_stream_body(root),
                               root->first_int_stream);

  /* read sub-stream data from disk, unzip it and buffer it */

  for (int_stream = root->first_int_stream;
       int_stream;
       int_stream = ((packed_int_private_t*)int_stream->private_data)->next)
    {
      apr_size_t offset = 0;
      SVN_ERR(read_stream_data(stream,
                               packed_int_stream_length(int_stream),
                               uncompressed, compressed));
      unflatten_int_stream(int_stream, uncompressed, &offset);
    }

  for (byte_stream = root->first_byte_stream;
       byte_stream;
       byte_stream = byte_stream->next)
    {
      apr_size_t offset = 0;
      SVN_ERR(read_stream_data(stream,
                               packed_byte_stream_length(byte_stream),
                               uncompressed, compressed));
      unflatten_byte_stream(byte_stream, uncompressed, &offset);
    }

  *root_p = root;
  return SVN_NO_ERROR;
}
