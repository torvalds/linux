/* index.c indexing support for FSFS support
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

#include "svn_io.h"
#include "svn_pools.h"
#include "svn_sorts.h"

#include "svn_private_config.h"

#include "private/svn_sorts_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_temp_serializer.h"

#include "index.h"
#include "pack.h"
#include "temp_serializer.h"
#include "util.h"
#include "fs_fs.h"

#include "../libsvn_fs/fs-loader.h"

/* maximum length of a uint64 in an 7/8b encoding */
#define ENCODED_INT_LENGTH 10

/* APR is missing an APR_OFF_T_MAX.  So, define one.  We will use it to
 * limit file offsets stored in the indexes.
 *
 * We assume that everything shorter than 64 bits, it is at least 32 bits.
 * We also assume that the type is always signed meaning we only have an
 * effective positive range of 63 or 31 bits, respectively.
 */
static
const apr_uint64_t off_t_max = (sizeof(apr_off_t) == sizeof(apr_int64_t))
                             ? APR_INT64_MAX
                             : APR_INT32_MAX;

/* We store P2L proto-index entries as 6 values, 64 bits each on disk.
 * See also svn_fs_fs__p2l_proto_index_add_entry().
 */
#define P2L_PROTO_INDEX_ENTRY_SIZE (6 * sizeof(apr_uint64_t))

/* We put this string in front of the L2P index header. */
#define L2P_STREAM_PREFIX "L2P-INDEX\n"

/* We put this string in front of the P2L index header. */
#define P2L_STREAM_PREFIX "P2L-INDEX\n"

/* Size of the buffer that will fit the index header prefixes. */
#define STREAM_PREFIX_LEN MAX(sizeof(L2P_STREAM_PREFIX), \
                              sizeof(P2L_STREAM_PREFIX))

/* Page tables in the log-to-phys index file exclusively contain entries
 * of this type to describe position and size of a given page.
 */
typedef struct l2p_page_table_entry_t
{
  /* global offset on the page within the index file */
  apr_uint64_t offset;

  /* number of mapping entries in that page */
  apr_uint32_t entry_count;

  /* size of the page on disk (in the index file) */
  apr_uint32_t size;
} l2p_page_table_entry_t;

/* Master run-time data structure of an log-to-phys index.  It contains
 * the page tables of every revision covered by that index - but not the
 * pages themselves.
 */
typedef struct l2p_header_t
{
  /* first revision covered by this index */
  svn_revnum_t first_revision;

  /* number of revisions covered */
  apr_size_t revision_count;

  /* (max) number of entries per page */
  apr_uint32_t page_size;

  /* indexes into PAGE_TABLE that mark the first page of the respective
   * revision.  PAGE_TABLE_INDEX[REVISION_COUNT] points to the end of
   * PAGE_TABLE.
   */
  apr_size_t * page_table_index;

  /* Page table covering all pages in the index */
  l2p_page_table_entry_t * page_table;
} l2p_header_t;

/* Run-time data structure containing a single log-to-phys index page.
 */
typedef struct l2p_page_t
{
  /* number of entries in the OFFSETS array */
  apr_uint32_t entry_count;

  /* global file offsets (item index is the array index) within the
   * packed or non-packed rev file.  Offset will be -1 for unused /
   * invalid item index values. */
  apr_uint64_t *offsets;
} l2p_page_t;

/* All of the log-to-phys proto index file consist of entries of this type.
 */
typedef struct l2p_proto_entry_t
{
  /* phys offset + 1 of the data container. 0 for "new revision" entries. */
  apr_uint64_t offset;

  /* corresponding item index. 0 for "new revision" entries. */
  apr_uint64_t item_index;
} l2p_proto_entry_t;

/* Master run-time data structure of an phys-to-log index.  It contains
 * an array with one offset value for each rev file cluster.
 */
typedef struct p2l_header_t
{
  /* first revision covered by the index (and rev file) */
  svn_revnum_t first_revision;

  /* number of bytes in the rev files covered by each p2l page */
  apr_uint64_t page_size;

  /* number of pages / clusters in that rev file */
  apr_size_t page_count;

  /* number of bytes in the rev file */
  apr_uint64_t file_size;

  /* offsets of the pages / cluster descriptions within the index file */
  apr_off_t *offsets;
} p2l_header_t;

/*
 * packed stream
 *
 * This is a utility object that will read files containing 7b/8b encoded
 * unsigned integers.  It decodes them in batches to minimize overhead
 * and supports random access to random file locations.
 */

/* How many numbers we will pre-fetch and buffer in a packed number stream.
 */
enum { MAX_NUMBER_PREFETCH = 64 };

/* Prefetched number entry in a packed number stream.
 */
typedef struct value_position_pair_t
{
  /* prefetched number */
  apr_uint64_t value;

  /* number of bytes read, *including* this number, since the buffer start */
  apr_size_t total_len;
} value_position_pair_t;

/* State of a prefetching packed number stream.  It will read compressed
 * index data efficiently and present it as a series of non-packed uint64.
 */
struct svn_fs_fs__packed_number_stream_t
{
  /* underlying data file containing the packed values */
  apr_file_t *file;

  /* Offset within FILE at which the stream data starts
   * (i.e. which offset will reported as offset 0 by packed_stream_offset). */
  apr_off_t stream_start;

  /* First offset within FILE after the stream data.
   * Attempts to read beyond this will cause an "Unexpected End Of Stream"
   * error. */
  apr_off_t stream_end;

  /* number of used entries in BUFFER (starting at index 0) */
  apr_size_t used;

  /* index of the next number to read from the BUFFER (0 .. USED).
   * If CURRENT == USED, we need to read more data upon get() */
  apr_size_t current;

  /* offset in FILE from which the first entry in BUFFER has been read */
  apr_off_t start_offset;

  /* offset in FILE from which the next number has to be read */
  apr_off_t next_offset;

  /* read the file in chunks of this size */
  apr_size_t block_size;

  /* pool to be used for file ops etc. */
  apr_pool_t *pool;

  /* buffer for prefetched values */
  value_position_pair_t buffer[MAX_NUMBER_PREFETCH];
};

/* Return an svn_error_t * object for error ERR on STREAM with the given
 * MESSAGE string.  The latter must have a placeholder for the index file
 * name ("%s") and the current read offset (e.g. "0x%lx").
 */
static svn_error_t *
stream_error_create(svn_fs_fs__packed_number_stream_t *stream,
                    apr_status_t err,
                    const char *message)
{
  const char *file_name;
  apr_off_t offset;
  SVN_ERR(svn_io_file_name_get(&file_name, stream->file,
                               stream->pool));
  SVN_ERR(svn_io_file_get_offset(&offset, stream->file, stream->pool));

  return svn_error_createf(err, NULL, message, file_name,
                           apr_psprintf(stream->pool,
                                        "%" APR_UINT64_T_HEX_FMT,
                                        (apr_uint64_t)offset));
}

/* Read up to MAX_NUMBER_PREFETCH numbers from the STREAM->NEXT_OFFSET in
 * STREAM->FILE and buffer them.
 *
 * We don't want GCC and others to inline this (infrequently called)
 * function into packed_stream_get() because it prevents the latter from
 * being inlined itself.
 */
SVN__PREVENT_INLINE
static svn_error_t *
packed_stream_read(svn_fs_fs__packed_number_stream_t *stream)
{
  unsigned char buffer[MAX_NUMBER_PREFETCH];
  apr_size_t bytes_read = 0;
  apr_size_t i;
  value_position_pair_t *target;
  apr_off_t block_start = 0;
  apr_off_t block_left = 0;
  apr_status_t err;

  /* all buffered data will have been read starting here */
  stream->start_offset = stream->next_offset;

  /* packed numbers are usually not aligned to MAX_NUMBER_PREFETCH blocks,
   * i.e. the last number has been incomplete (and not buffered in stream)
   * and need to be re-read.  Therefore, always correct the file pointer.
   */
  SVN_ERR(svn_io_file_aligned_seek(stream->file, stream->block_size,
                                   &block_start, stream->next_offset,
                                   stream->pool));

  /* prefetch at least one number but, if feasible, don't cross block
   * boundaries.  This shall prevent jumping back and forth between two
   * blocks because the extra data was not actually request _now_.
   */
  bytes_read = sizeof(buffer);
  block_left = stream->block_size - (stream->next_offset - block_start);
  if (block_left >= 10 && block_left < bytes_read)
    bytes_read = (apr_size_t)block_left;

  /* Don't read beyond the end of the file section that belongs to this
   * index / stream. */
  bytes_read = (apr_size_t)MIN(bytes_read,
                               stream->stream_end - stream->next_offset);

  err = apr_file_read(stream->file, buffer, &bytes_read);
  if (err && !APR_STATUS_IS_EOF(err))
    return stream_error_create(stream, err,
      _("Can't read index file '%s' at offset 0x%s"));

  /* if the last number is incomplete, trim it from the buffer */
  while (bytes_read > 0 && buffer[bytes_read-1] >= 0x80)
    --bytes_read;

  /* we call read() only if get() requires more data.  So, there must be
   * at least *one* further number. */
  if SVN__PREDICT_FALSE(bytes_read == 0)
    return stream_error_create(stream, err,
      _("Unexpected end of index file %s at offset 0x%s"));

  /* parse file buffer and expand into stream buffer */
  target = stream->buffer;
  for (i = 0; i < bytes_read;)
    {
      if (buffer[i] < 0x80)
        {
          /* numbers < 128 are relatively frequent and particularly easy
           * to decode.  Give them special treatment. */
          target->value = buffer[i];
          ++i;
          target->total_len = i;
          ++target;
        }
      else
        {
          apr_uint64_t value = 0;
          apr_uint64_t shift = 0;
          while (buffer[i] >= 0x80)
            {
              value += ((apr_uint64_t)buffer[i] & 0x7f) << shift;
              shift += 7;
              ++i;
            }

          target->value = value + ((apr_uint64_t)buffer[i] << shift);
          ++i;
          target->total_len = i;
          ++target;

          /* let's catch corrupted data early.  It would surely cause
           * havoc further down the line. */
          if SVN__PREDICT_FALSE(shift > 8 * sizeof(value))
            return svn_error_createf(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                                     _("Corrupt index: number too large"));
       }
    }

  /* update stream state */
  stream->used = target - stream->buffer;
  stream->next_offset = stream->start_offset + i;
  stream->current = 0;

  return SVN_NO_ERROR;
}

/* Create and open a packed number stream reading from offsets START to
 * END in FILE and return it in *STREAM.  Access the file in chunks of
 * BLOCK_SIZE bytes.  Expect the stream to be prefixed by STREAM_PREFIX.
 * Allocate *STREAM in RESULT_POOL and use SCRATCH_POOL for temporaries.
 */
static svn_error_t *
packed_stream_open(svn_fs_fs__packed_number_stream_t **stream,
                   apr_file_t *file,
                   apr_off_t start,
                   apr_off_t end,
                   const char *stream_prefix,
                   apr_size_t block_size,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  char buffer[STREAM_PREFIX_LEN + 1] = { 0 };
  apr_size_t len = strlen(stream_prefix);
  svn_fs_fs__packed_number_stream_t *result;

  /* If this is violated, we forgot to adjust STREAM_PREFIX_LEN after
   * changing the index header prefixes. */
  SVN_ERR_ASSERT(len < sizeof(buffer));

  /* Read the header prefix and compare it with the expected prefix */
  SVN_ERR(svn_io_file_aligned_seek(file, block_size, NULL, start,
                                   scratch_pool));
  SVN_ERR(svn_io_file_read_full2(file, buffer, len, NULL, NULL,
                                 scratch_pool));

  if (strncmp(buffer, stream_prefix, len))
    return svn_error_createf(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                             _("Index stream header prefix mismatch.\n"
                               "  expected: %s"
                               "  found: %s"), stream_prefix, buffer);

  /* Construct the actual stream object. */
  result = apr_palloc(result_pool, sizeof(*result));

  result->pool = result_pool;
  result->file = file;
  result->stream_start = start + len;
  result->stream_end = end;

  result->used = 0;
  result->current = 0;
  result->start_offset = result->stream_start;
  result->next_offset = result->stream_start;
  result->block_size = block_size;

  *stream = result;

  return SVN_NO_ERROR;
}

/*
 * The forced inline is required for performance reasons:  This is a very
 * hot code path (called for every item we read) but e.g. GCC would rather
 * chose to inline packed_stream_read() here, preventing packed_stream_get
 * from being inlined itself.
 */
SVN__FORCE_INLINE
static svn_error_t*
packed_stream_get(apr_uint64_t *value,
                  svn_fs_fs__packed_number_stream_t *stream)
{
  if (stream->current == stream->used)
    SVN_ERR(packed_stream_read(stream));

  *value = stream->buffer[stream->current].value;
  ++stream->current;

  return SVN_NO_ERROR;
}

/* Navigate STREAM to packed stream offset OFFSET.  There will be no checks
 * whether the given OFFSET is valid.
 */
static void
packed_stream_seek(svn_fs_fs__packed_number_stream_t *stream,
                   apr_off_t offset)
{
  apr_off_t file_offset = offset + stream->stream_start;

  if (   stream->used == 0
      || offset < stream->start_offset
      || offset >= stream->next_offset)
    {
      /* outside buffered data.  Next get() will read() from OFFSET. */
      stream->start_offset = file_offset;
      stream->next_offset = file_offset;
      stream->current = 0;
      stream->used = 0;
    }
  else
    {
      /* Find the suitable location in the stream buffer.
       * Since our buffer is small, it is efficient enough to simply scan
       * it for the desired position. */
      apr_size_t i;
      for (i = 0; i < stream->used; ++i)
        if (stream->buffer[i].total_len > file_offset - stream->start_offset)
          break;

      stream->current = i;
    }
}

/* Return the packed stream offset of at which the next number in the stream
 * can be found.
 */
static apr_off_t
packed_stream_offset(svn_fs_fs__packed_number_stream_t *stream)
{
  apr_off_t file_offset
       = stream->current == 0
       ? stream->start_offset
       : stream->buffer[stream->current-1].total_len + stream->start_offset;

  return file_offset - stream->stream_start;
}

/* Encode VALUE as 7/8b into P and return the number of bytes written.
 * This will be used when _writing_ packed data.  packed_stream_* is for
 * read operations only.
 */
static apr_size_t
encode_uint(unsigned char *p, apr_uint64_t value)
{
  unsigned char *start = p;
  while (value >= 0x80)
    {
      *p = (unsigned char)((value % 0x80) + 0x80);
      value /= 0x80;
      ++p;
    }

  *p = (unsigned char)(value % 0x80);
  return (p - start) + 1;
}

/* Encode VALUE as 7/8b into P and return the number of bytes written.
 * This maps signed ints onto unsigned ones.
 */
static apr_size_t
encode_int(unsigned char *p, apr_int64_t value)
{
  return encode_uint(p, (apr_uint64_t)(value < 0 ? -1 - 2*value : 2*value));
}

/* Append VALUE to STREAM in 7/8b encoding.
 */
static svn_error_t *
stream_write_encoded(svn_stream_t *stream,
                     apr_uint64_t value)
{
  unsigned char encoded[ENCODED_INT_LENGTH];

  apr_size_t len = encode_uint(encoded, value);
  return svn_error_trace(svn_stream_write(stream, (char *)encoded, &len));
}

/* Map unsigned VALUE back to signed integer.
 */
static apr_int64_t
decode_int(apr_uint64_t value)
{
  return (apr_int64_t)(value % 2 ? -1 - value / 2 : value / 2);
}

/* Write VALUE to the PROTO_INDEX file, using SCRATCH_POOL for temporary
 * allocations.
 *
 * The point of this function is to ensure an architecture-independent
 * proto-index file format.  All data is written as unsigned 64 bits ints
 * in little endian byte order.  64 bits is the largest portable integer
 * we have and unsigned values have well-defined conversions in C.
 */
static svn_error_t *
write_uint64_to_proto_index(apr_file_t *proto_index,
                            apr_uint64_t value,
                            apr_pool_t *scratch_pool)
{
  apr_byte_t buffer[sizeof(value)];
  int i;
  apr_size_t written;

  /* Split VALUE into 8 bytes using LE ordering. */
  for (i = 0; i < sizeof(buffer); ++i)
    {
      /* Unsigned conversions are well-defined ... */
      buffer[i] = (apr_byte_t)value;
      value >>= CHAR_BIT;
    }

  /* Write it all to disk. */
  SVN_ERR(svn_io_file_write_full(proto_index, buffer, sizeof(buffer),
                                 &written, scratch_pool));
  SVN_ERR_ASSERT(written == sizeof(buffer));

  return SVN_NO_ERROR;
}

/* Read one unsigned 64 bit value from PROTO_INDEX file and return it in
 * *VALUE_P.  If EOF is NULL, error out when trying to read beyond EOF.
 * Use SCRATCH_POOL for temporary allocations.
 *
 * This function is the inverse to write_uint64_to_proto_index (see there),
 * reading the external LE byte order and convert it into host byte order.
 */
static svn_error_t *
read_uint64_from_proto_index(apr_file_t *proto_index,
                             apr_uint64_t *value_p,
                             svn_boolean_t *eof,
                             apr_pool_t *scratch_pool)
{
  apr_byte_t buffer[sizeof(*value_p)];
  apr_size_t bytes_read;

  /* Read the full 8 bytes or our 64 bit value, unless we hit EOF.
   * Assert that we never read partial values. */
  SVN_ERR(svn_io_file_read_full2(proto_index, buffer, sizeof(buffer),
                                 &bytes_read, eof, scratch_pool));
  SVN_ERR_ASSERT((eof && *eof) || bytes_read == sizeof(buffer));

  /* If we did not hit EOF, reconstruct the uint64 value and return it. */
  if (!eof || !*eof)
    {
      int i;
      apr_uint64_t value;

      /* This could only overflow if CHAR_BIT had a value that is not
       * a divisor of 64. */
      value = 0;
      for (i = sizeof(buffer) - 1; i >= 0; --i)
        value = (value << CHAR_BIT) + buffer[i];

      *value_p = value;
    }

  return SVN_NO_ERROR;
}

/* Convenience function similar to read_uint64_from_proto_index, but returns
 * an uint32 value in VALUE_P.  Return an error if the value does not fit.
 */
static svn_error_t *
read_uint32_from_proto_index(apr_file_t *proto_index,
                             apr_uint32_t *value_p,
                             svn_boolean_t *eof,
                             apr_pool_t *scratch_pool)
{
  apr_uint64_t value;
  SVN_ERR(read_uint64_from_proto_index(proto_index, &value, eof,
                                       scratch_pool));
  if (!eof || !*eof)
    {
      if (value > APR_UINT32_MAX)
        return svn_error_createf(SVN_ERR_FS_INDEX_OVERFLOW, NULL,
                                _("UINT32 0x%s too large, max = 0x%s"),
                                apr_psprintf(scratch_pool,
                                             "%" APR_UINT64_T_HEX_FMT,
                                             value),
                                apr_psprintf(scratch_pool,
                                             "%" APR_UINT64_T_HEX_FMT,
                                             (apr_uint64_t)APR_UINT32_MAX));

      /* This conversion is not lossy because the value can be represented
       * in the target type. */
      *value_p = (apr_uint32_t)value;
    }

  return SVN_NO_ERROR;
}

/* Convenience function similar to read_uint64_from_proto_index, but returns
 * an off_t value in VALUE_P.  Return an error if the value does not fit.
 */
static svn_error_t *
read_off_t_from_proto_index(apr_file_t *proto_index,
                            apr_off_t *value_p,
                            svn_boolean_t *eof,
                            apr_pool_t *scratch_pool)
{
  apr_uint64_t value;
  SVN_ERR(read_uint64_from_proto_index(proto_index, &value, eof,
                                       scratch_pool));
  if (!eof || !*eof)
    {
      if (value > off_t_max)
        return svn_error_createf(SVN_ERR_FS_INDEX_OVERFLOW, NULL,
                                _("File offset 0x%s too large, max = 0x%s"),
                                apr_psprintf(scratch_pool,
                                             "%" APR_UINT64_T_HEX_FMT,
                                             value),
                                apr_psprintf(scratch_pool,
                                             "%" APR_UINT64_T_HEX_FMT,
                                             off_t_max));

      /* Shortening conversion from unsigned to signed int is well-defined
       * and not lossy in C because the value can be represented in the
       * target type. */
      *value_p = (apr_off_t)value;
    }

  return SVN_NO_ERROR;
}

/*
 * log-to-phys index
 */

/* Append ENTRY to log-to-phys PROTO_INDEX file.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
write_l2p_entry_to_proto_index(apr_file_t *proto_index,
                               l2p_proto_entry_t entry,
                               apr_pool_t *scratch_pool)
{
  SVN_ERR(write_uint64_to_proto_index(proto_index, entry.offset,
                                      scratch_pool));
  SVN_ERR(write_uint64_to_proto_index(proto_index, entry.item_index,
                                      scratch_pool));

  return SVN_NO_ERROR;
}

/* Read *ENTRY from log-to-phys PROTO_INDEX file and indicate end-of-file
 * in *EOF, or error out in that case if EOF is NULL.  *ENTRY is in an
 * undefined state if an end-of-file occurred.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
read_l2p_entry_from_proto_index(apr_file_t *proto_index,
                                l2p_proto_entry_t *entry,
                                svn_boolean_t *eof,
                                apr_pool_t *scratch_pool)
{
  SVN_ERR(read_uint64_from_proto_index(proto_index, &entry->offset, eof,
                                       scratch_pool));
  SVN_ERR(read_uint64_from_proto_index(proto_index, &entry->item_index, eof,
                                       scratch_pool));

  return SVN_NO_ERROR;
}

/* Write the log-2-phys index page description for the l2p_page_entry_t
 * array ENTRIES, starting with element START up to but not including END.
 * Write the resulting representation into BUFFER.  Use SCRATCH_POOL for
 * temporary allocations.
 */
static svn_error_t *
encode_l2p_page(apr_array_header_t *entries,
                int start,
                int end,
                svn_spillbuf_t *buffer,
                apr_pool_t *scratch_pool)
{
  unsigned char encoded[ENCODED_INT_LENGTH];
  int i;
  const apr_uint64_t *values = (const apr_uint64_t *)entries->elts;
  apr_uint64_t last_value = 0;

  /* encode items */
  for (i = start; i < end; ++i)
    {
      apr_int64_t diff = values[i] - last_value;
      last_value = values[i];
      SVN_ERR(svn_spillbuf__write(buffer, (const char *)encoded,
                                  encode_int(encoded, diff), scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__l2p_proto_index_open(apr_file_t **proto_index,
                                const char *file_name,
                                apr_pool_t *result_pool)
{
  SVN_ERR(svn_io_file_open(proto_index, file_name, APR_READ | APR_WRITE
                           | APR_CREATE | APR_APPEND | APR_BUFFERED,
                           APR_OS_DEFAULT, result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__l2p_proto_index_add_revision(apr_file_t *proto_index,
                                        apr_pool_t *scratch_pool)
{
  l2p_proto_entry_t entry;
  entry.offset = 0;
  entry.item_index = 0;

  return svn_error_trace(write_l2p_entry_to_proto_index(proto_index, entry,
                                                        scratch_pool));
}

svn_error_t *
svn_fs_fs__l2p_proto_index_add_entry(apr_file_t *proto_index,
                                     apr_off_t offset,
                                     apr_uint64_t item_index,
                                     apr_pool_t *scratch_pool)
{
  l2p_proto_entry_t entry;

  /* make sure the conversion to uint64 works */
  SVN_ERR_ASSERT(offset >= -1);

  /* we support offset '-1' as a "not used" indication */
  entry.offset = (apr_uint64_t)offset + 1;

  /* make sure we can use item_index as an array index when building the
   * final index file */
  SVN_ERR_ASSERT(item_index < UINT_MAX / 2);
  entry.item_index = item_index;

  return svn_error_trace(write_l2p_entry_to_proto_index(proto_index, entry,
                                                        scratch_pool));
}

svn_error_t *
svn_fs_fs__l2p_index_append(svn_checksum_t **checksum,
                            svn_fs_t *fs,
                            apr_file_t *index_file,
                            const char *proto_file_name,
                            svn_revnum_t revision,
                            apr_pool_t * result_pool,
                            apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_file_t *proto_index = NULL;
  svn_stream_t *stream;
  int i;
  apr_uint64_t entry;
  svn_boolean_t eof = FALSE;

  int last_page_count = 0;          /* total page count at the start of
                                       the current revision */

  /* temporary data structures that collect the data which will be moved
     to the target file in a second step */
  apr_pool_t *local_pool = svn_pool_create(scratch_pool);
  apr_pool_t *iterpool = svn_pool_create(local_pool);
  apr_array_header_t *page_counts
    = apr_array_make(local_pool, 16, sizeof(apr_uint64_t));
  apr_array_header_t *page_sizes
    = apr_array_make(local_pool, 16, sizeof(apr_uint64_t));
  apr_array_header_t *entry_counts
    = apr_array_make(local_pool, 16, sizeof(apr_uint64_t));

  /* collect the item offsets and sub-item value for the current revision */
  apr_array_header_t *entries
    = apr_array_make(local_pool, 256, sizeof(apr_uint64_t));

  /* 64k blocks, spill after 16MB */
  svn_spillbuf_t *buffer
    = svn_spillbuf__create(0x10000, 0x1000000, local_pool);

  /* Paranoia check that makes later casting to int32 safe.
   * The current implementation is limited to 2G entries per page. */
  if (ffd->l2p_page_size > APR_INT32_MAX)
    return svn_error_createf(SVN_ERR_FS_INDEX_OVERFLOW , NULL,
                            _("L2P index page size  %s"
                              " exceeds current limit of 2G entries"),
                            apr_psprintf(local_pool, "%" APR_UINT64_T_FMT,
                                         ffd->l2p_page_size));

  /* start at the beginning of the source file */
  SVN_ERR(svn_io_file_open(&proto_index, proto_file_name,
                           APR_READ | APR_CREATE | APR_BUFFERED,
                           APR_OS_DEFAULT, scratch_pool));

  /* process all entries until we fail due to EOF */
  for (entry = 0; !eof; ++entry)
    {
      l2p_proto_entry_t proto_entry;

      /* (attempt to) read the next entry from the source */
      SVN_ERR(read_l2p_entry_from_proto_index(proto_index, &proto_entry,
                                              &eof, local_pool));

      /* handle new revision */
      if ((entry > 0 && proto_entry.offset == 0) || eof)
        {
          /* dump entries, grouped into pages */

          int entry_count = 0;
          for (i = 0; i < entries->nelts; i += entry_count)
            {
              /* 1 page with up to L2P_PAGE_SIZE entries.
               * fsfs.conf settings validation guarantees this to fit into
               * our address space. */
              apr_uint64_t last_buffer_size
                = (apr_uint64_t)svn_spillbuf__get_size(buffer);

              svn_pool_clear(iterpool);

              entry_count = ffd->l2p_page_size < entries->nelts - i
                          ? (int)ffd->l2p_page_size
                          : entries->nelts - i;
              SVN_ERR(encode_l2p_page(entries, i, i + entry_count,
                                      buffer, iterpool));

              APR_ARRAY_PUSH(entry_counts, apr_uint64_t) = entry_count;
              APR_ARRAY_PUSH(page_sizes, apr_uint64_t)
                = svn_spillbuf__get_size(buffer) - last_buffer_size;
            }

          apr_array_clear(entries);

          /* store the number of pages in this revision */
          APR_ARRAY_PUSH(page_counts, apr_uint64_t)
            = page_sizes->nelts - last_page_count;

          last_page_count = page_sizes->nelts;
        }
      else
        {
          int idx;

          /* store the mapping in our array */
          if (proto_entry.item_index > APR_INT32_MAX)
            return svn_error_createf(SVN_ERR_FS_INDEX_OVERFLOW , NULL,
                                    _("Item index %s too large "
                                      "in l2p proto index for revision %ld"),
                                    apr_psprintf(local_pool, "%" APR_UINT64_T_FMT,
                                                 proto_entry.item_index),
                                    revision + page_counts->nelts);

          idx = (int)proto_entry.item_index;
          while (idx >= entries->nelts)
            APR_ARRAY_PUSH(entries, apr_uint64_t) = 0;

          APR_ARRAY_IDX(entries, idx, apr_uint64_t) = proto_entry.offset;
        }
    }

  /* close the source file */
  SVN_ERR(svn_io_file_close(proto_index, local_pool));

  /* Paranoia check that makes later casting to int32 safe.
   * The current implementation is limited to 2G pages per index. */
  if (page_counts->nelts > APR_INT32_MAX)
    return svn_error_createf(SVN_ERR_FS_INDEX_OVERFLOW , NULL,
                            _("L2P index page count  %d"
                              " exceeds current limit of 2G pages"),
                            page_counts->nelts);

  /* open target stream. */
  stream = svn_stream_checksummed2(svn_stream_from_aprfile2(index_file, TRUE,
                                                            local_pool),
                                   NULL, checksum, svn_checksum_md5, FALSE,
                                   result_pool);


  /* write header info */
  SVN_ERR(svn_stream_puts(stream, L2P_STREAM_PREFIX));
  SVN_ERR(stream_write_encoded(stream, revision));
  SVN_ERR(stream_write_encoded(stream, ffd->l2p_page_size));
  SVN_ERR(stream_write_encoded(stream, page_counts->nelts));
  SVN_ERR(stream_write_encoded(stream, page_sizes->nelts));

  /* write the revision table */
  for (i = 0; i < page_counts->nelts; ++i)
    {
      apr_uint64_t value = APR_ARRAY_IDX(page_counts, i, apr_uint64_t);
      SVN_ERR(stream_write_encoded(stream, value));
    }

  /* write the page table */
  for (i = 0; i < page_sizes->nelts; ++i)
    {
      apr_uint64_t value = APR_ARRAY_IDX(page_sizes, i, apr_uint64_t);
      SVN_ERR(stream_write_encoded(stream, value));
      value = APR_ARRAY_IDX(entry_counts, i, apr_uint64_t);
      SVN_ERR(stream_write_encoded(stream, value));
    }

  /* append page contents and implicitly close STREAM */
  SVN_ERR(svn_stream_copy3(svn_stream__from_spillbuf(buffer, local_pool),
                           stream, NULL, NULL, local_pool));

  svn_pool_destroy(local_pool);

  return SVN_NO_ERROR;
}

/* If REV_FILE->L2P_STREAM is NULL, create a new stream for the log-to-phys
 * index for REVISION in FS and return it in REV_FILE.
 */
static svn_error_t *
auto_open_l2p_index(svn_fs_fs__revision_file_t *rev_file,
                    svn_fs_t *fs,
                    svn_revnum_t revision)
{
  if (rev_file->l2p_stream == NULL)
    {
      fs_fs_data_t *ffd = fs->fsap_data;

      SVN_ERR(svn_fs_fs__auto_read_footer(rev_file));
      SVN_ERR(packed_stream_open(&rev_file->l2p_stream,
                                 rev_file->file,
                                 rev_file->l2p_offset,
                                 rev_file->p2l_offset,
                                 L2P_STREAM_PREFIX,
                                 (apr_size_t)ffd->block_size,
                                 rev_file->pool,
                                 rev_file->pool));
    }

  return SVN_NO_ERROR;
}

/* Read the header data structure of the log-to-phys index for REVISION
 * in FS and return it in *HEADER, allocated in RESULT_POOL.  Use REV_FILE
 * to access on-disk data.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
get_l2p_header_body(l2p_header_t **header,
                    svn_fs_fs__revision_file_t *rev_file,
                    svn_fs_t *fs,
                    svn_revnum_t revision,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_uint64_t value;
  apr_size_t i;
  apr_size_t page, page_count;
  apr_off_t offset;
  l2p_header_t *result = apr_pcalloc(result_pool, sizeof(*result));
  apr_size_t page_table_index;
  svn_revnum_t next_rev;

  pair_cache_key_t key;
  key.revision = rev_file->start_revision;
  key.second = rev_file->is_packed;

  SVN_ERR(auto_open_l2p_index(rev_file, fs, revision));
  packed_stream_seek(rev_file->l2p_stream, 0);

  /* Read the table sizes.  Check the data for plausibility and
   * consistency with other bits. */
  SVN_ERR(packed_stream_get(&value, rev_file->l2p_stream));
  result->first_revision = (svn_revnum_t)value;
  if (result->first_revision != rev_file->start_revision)
    return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                  _("Index rev / pack file revision numbers do not match"));

  SVN_ERR(packed_stream_get(&value, rev_file->l2p_stream));
  result->page_size = (apr_uint32_t)value;
  if (!result->page_size || (result->page_size & (result->page_size - 1)))
    return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                            _("L2P index page size is not a power of two"));

  SVN_ERR(packed_stream_get(&value, rev_file->l2p_stream));
  result->revision_count = (int)value;
  if (   result->revision_count != 1
      && result->revision_count != (apr_uint64_t)ffd->max_files_per_dir)
    return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                            _("Invalid number of revisions in L2P index"));

  SVN_ERR(packed_stream_get(&value, rev_file->l2p_stream));
  page_count = (apr_size_t)value;
  if (page_count < result->revision_count)
    return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                            _("Fewer L2P index pages than revisions"));
  if (page_count > (rev_file->p2l_offset - rev_file->l2p_offset) / 2)
    return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                            _("L2P index page count implausibly large"));

  next_rev = result->first_revision + (svn_revnum_t)result->revision_count;
  if (result->first_revision > revision || next_rev <= revision)
    return svn_error_createf(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                      _("Corrupt L2P index for r%ld only covers r%ld:%ld"),
                      revision, result->first_revision, next_rev);

  /* allocate the page tables */
  result->page_table
    = apr_pcalloc(result_pool, page_count * sizeof(*result->page_table));
  result->page_table_index
    = apr_pcalloc(result_pool, (result->revision_count + 1)
                             * sizeof(*result->page_table_index));

  /* read per-revision page table sizes (i.e. number of pages per rev) */
  page_table_index = 0;
  result->page_table_index[0] = page_table_index;

  for (i = 0; i < result->revision_count; ++i)
    {
      SVN_ERR(packed_stream_get(&value, rev_file->l2p_stream));
      if (value == 0)
        return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                                _("Revision with no L2P index pages"));

      page_table_index += (apr_size_t)value;
      if (page_table_index > page_count)
        return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                                _("L2P page table exceeded"));

      result->page_table_index[i+1] = page_table_index;
    }

  if (page_table_index != page_count)
    return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                 _("Revisions do not cover the full L2P index page table"));

  /* read actual page tables */
  for (page = 0; page < page_count; ++page)
    {
      SVN_ERR(packed_stream_get(&value, rev_file->l2p_stream));
      if (value == 0)
        return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                                _("Empty L2P index page"));

      result->page_table[page].size = (apr_uint32_t)value;
      SVN_ERR(packed_stream_get(&value, rev_file->l2p_stream));
      if (value > result->page_size)
        return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                                _("Page exceeds L2P index page size"));

      result->page_table[page].entry_count = (apr_uint32_t)value;
    }

  /* correct the page description offsets */
  offset = packed_stream_offset(rev_file->l2p_stream);
  for (page = 0; page < page_count; ++page)
    {
      result->page_table[page].offset = offset;
      offset += result->page_table[page].size;
    }

  /* return and cache the header */
  *header = result;
  SVN_ERR(svn_cache__set(ffd->l2p_header_cache, &key, result, scratch_pool));

  return SVN_NO_ERROR;
}

/* Data structure that describes which l2p page info shall be extracted
 * from the cache and contains the fields that receive the result.
 */
typedef struct l2p_page_info_baton_t
{
  /* input data: we want the page covering (REVISION,ITEM_INDEX) */
  svn_revnum_t revision;
  apr_uint64_t item_index;

  /* out data */
  /* page location and size of the page within the l2p index file */
  l2p_page_table_entry_t entry;

  /* page number within the pages for REVISION (not l2p index global!) */
  apr_uint32_t page_no;

  /* offset of ITEM_INDEX within that page */
  apr_uint32_t page_offset;

  /* revision identifying the l2p index file, also the first rev in that */
  svn_revnum_t first_revision;
} l2p_page_info_baton_t;


/* Utility function that copies the info requested by BATON->REVISION and
 * BATON->ITEM_INDEX and from HEADER and PAGE_TABLE into the output fields
 * of *BATON.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
l2p_page_info_copy(l2p_page_info_baton_t *baton,
                   const l2p_header_t *header,
                   const l2p_page_table_entry_t *page_table,
                   const apr_size_t *page_table_index,
                   apr_pool_t *scratch_pool)
{
  /* revision offset within the index file */
  apr_size_t rel_revision = baton->revision - header->first_revision;
  if (rel_revision >= header->revision_count)
    return svn_error_createf(SVN_ERR_FS_INDEX_REVISION , NULL,
                             _("Revision %ld not covered by item index"),
                             baton->revision);

  /* select the relevant page */
  if (baton->item_index < header->page_size)
    {
      /* most revs fit well into a single page */
      baton->page_offset = (apr_uint32_t)baton->item_index;
      baton->page_no = 0;
      baton->entry = page_table[page_table_index[rel_revision]];
    }
  else
    {
      const l2p_page_table_entry_t *first_entry;
      const l2p_page_table_entry_t *last_entry;
      apr_uint64_t max_item_index;

      /* range of pages for this rev */
      first_entry = page_table + page_table_index[rel_revision];
      last_entry = page_table + page_table_index[rel_revision + 1];

      /* do we hit a valid index page? */
      max_item_index =   (apr_uint64_t)header->page_size
                       * (last_entry - first_entry);
      if (baton->item_index >= max_item_index)
        return svn_error_createf(SVN_ERR_FS_INDEX_OVERFLOW , NULL,
                                _("Item index %s exceeds l2p limit "
                                  "of %s for revision %ld"),
                                apr_psprintf(scratch_pool,
                                             "%" APR_UINT64_T_FMT,
                                             baton->item_index),
                                apr_psprintf(scratch_pool,
                                             "%" APR_UINT64_T_FMT,
                                             max_item_index),
                                baton->revision);

      /* all pages are of the same size and full, except for the last one */
      baton->page_offset = (apr_uint32_t)(baton->item_index % header->page_size);
      baton->page_no = (apr_uint32_t)(baton->item_index / header->page_size);
      baton->entry = first_entry[baton->page_no];
    }

  baton->first_revision = header->first_revision;

  return SVN_NO_ERROR;
}

/* Implement svn_cache__partial_getter_func_t: copy the data requested in
 * l2p_page_info_baton_t *BATON from l2p_header_t *DATA into the output
 * fields in *BATON.
 */
static svn_error_t *
l2p_page_info_access_func(void **out,
                          const void *data,
                          apr_size_t data_len,
                          void *baton,
                          apr_pool_t *result_pool)
{
  /* resolve all pointer values of in-cache data */
  const l2p_header_t *header = data;
  const l2p_page_table_entry_t *page_table
    = svn_temp_deserializer__ptr(header,
                                 (const void *const *)&header->page_table);
  const apr_size_t *page_table_index
    = svn_temp_deserializer__ptr(header,
                           (const void *const *)&header->page_table_index);

  /* copy the info */
  return l2p_page_info_copy(baton, header, page_table, page_table_index,
                            result_pool);
}

/* Get the page info requested in *BATON from FS and set the output fields
 * in *BATON.  Use REV_FILE for on-disk file access.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
get_l2p_page_info(l2p_page_info_baton_t *baton,
                  svn_fs_fs__revision_file_t *rev_file,
                  svn_fs_t *fs,
                  apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  l2p_header_t *result;
  svn_boolean_t is_cached = FALSE;
  void *dummy = NULL;

  /* try to find the info in the cache */
  pair_cache_key_t key;
  key.revision = rev_file->start_revision;
  key.second = rev_file->is_packed;
  SVN_ERR(svn_cache__get_partial((void**)&dummy, &is_cached,
                                 ffd->l2p_header_cache, &key,
                                 l2p_page_info_access_func, baton,
                                 scratch_pool));
  if (is_cached)
    return SVN_NO_ERROR;

  /* read from disk, cache and copy the result */
  SVN_ERR(get_l2p_header_body(&result, rev_file, fs, baton->revision,
                              scratch_pool, scratch_pool));
  SVN_ERR(l2p_page_info_copy(baton, result, result->page_table,
                             result->page_table_index, scratch_pool));

  return SVN_NO_ERROR;
}

/* Data request structure used by l2p_page_table_access_func.
 */
typedef struct l2p_page_table_baton_t
{
  /* revision for which to read the page table */
  svn_revnum_t revision;

  /* page table entries (of type l2p_page_table_entry_t).
   * Must be created by caller and will be filled by callee. */
  apr_array_header_t *pages;
} l2p_page_table_baton_t;

/* Implement svn_cache__partial_getter_func_t: copy the data requested in
 * l2p_page_baton_t *BATON from l2p_page_t *DATA into BATON->PAGES and *OUT.
 */
static svn_error_t *
l2p_page_table_access_func(void **out,
                           const void *data,
                           apr_size_t data_len,
                           void *baton,
                           apr_pool_t *result_pool)
{
  /* resolve in-cache pointers */
  l2p_page_table_baton_t *table_baton = baton;
  const l2p_header_t *header = (const l2p_header_t *)data;
  const l2p_page_table_entry_t *page_table
    = svn_temp_deserializer__ptr(header,
                                 (const void *const *)&header->page_table);
  const apr_size_t *page_table_index
    = svn_temp_deserializer__ptr(header,
                           (const void *const *)&header->page_table_index);

  /* copy the revision's page table into BATON */
  apr_size_t rel_revision = table_baton->revision - header->first_revision;
  if (rel_revision < header->revision_count)
    {
      const l2p_page_table_entry_t *entry
        = page_table + page_table_index[rel_revision];
      const l2p_page_table_entry_t *last_entry
        = page_table + page_table_index[rel_revision + 1];

      for (; entry < last_entry; ++entry)
        APR_ARRAY_PUSH(table_baton->pages, l2p_page_table_entry_t)
          = *entry;
    }

  /* set output as a courtesy to the caller */
  *out = table_baton->pages;

  return SVN_NO_ERROR;
}

/* Read the l2p index page table for REVISION in FS from cache and return
 * it in PAGES.  The later must be provided by the caller (and can be
 * re-used); existing entries will be removed before writing the result.
 * If the data cannot be found in the cache, the result will be empty
 * (it never can be empty for a valid REVISION if the data is cached).
 * Use the info from REV_FILE to determine pack / rev file properties.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
get_l2p_page_table(apr_array_header_t *pages,
                   svn_fs_t *fs,
                   svn_fs_fs__revision_file_t *rev_file,
                   svn_revnum_t revision,
                   apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_boolean_t is_cached = FALSE;
  l2p_page_table_baton_t baton;

  pair_cache_key_t key;
  key.revision = rev_file->start_revision;
  key.second = rev_file->is_packed;

  apr_array_clear(pages);
  baton.revision = revision;
  baton.pages = pages;
  SVN_ERR(svn_cache__get_partial((void**)&pages, &is_cached,
                                 ffd->l2p_header_cache, &key,
                                 l2p_page_table_access_func, &baton,
                                 scratch_pool));

  return SVN_NO_ERROR;
}

/* From the log-to-phys index file starting at START_REVISION in FS, read
 * the mapping page identified by TABLE_ENTRY and return it in *PAGE.
 * Use REV_FILE to access on-disk files.
 * Use RESULT_POOL for allocations.
 */
static svn_error_t *
get_l2p_page(l2p_page_t **page,
             svn_fs_fs__revision_file_t *rev_file,
             svn_fs_t *fs,
             svn_revnum_t start_revision,
             l2p_page_table_entry_t *table_entry,
             apr_pool_t *result_pool)
{
  apr_uint32_t i;
  l2p_page_t *result = apr_pcalloc(result_pool, sizeof(*result));
  apr_uint64_t last_value = 0;

  /* open index file and select page */
  SVN_ERR(auto_open_l2p_index(rev_file, fs, start_revision));
  packed_stream_seek(rev_file->l2p_stream, table_entry->offset);

  /* initialize the page content */
  result->entry_count = table_entry->entry_count;
  result->offsets = apr_pcalloc(result_pool, result->entry_count
                                           * sizeof(*result->offsets));

  /* read all page entries (offsets in rev file and container sub-items) */
  for (i = 0; i < result->entry_count; ++i)
    {
      apr_uint64_t value = 0;
      SVN_ERR(packed_stream_get(&value, rev_file->l2p_stream));
      last_value += decode_int(value);
      result->offsets[i] = last_value - 1;
    }

  /* After reading all page entries, the read cursor must have moved by
   * TABLE_ENTRY->SIZE bytes. */
  if (   packed_stream_offset(rev_file->l2p_stream)
      != table_entry->offset + table_entry->size)
    return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                _("L2P actual page size does not match page table value."));

  *page = result;

  return SVN_NO_ERROR;
}

/* Utility function.  Read the l2p index pages for REVISION in FS from
 * REV_FILE and put them into the cache.  Skip page number EXLCUDED_PAGE_NO
 * (use -1 for 'skip none') and pages outside the MIN_OFFSET, MAX_OFFSET
 * range in the l2p index file.  The index is being identified by
 * FIRST_REVISION.  PAGES is a scratch container provided by the caller.
 * SCRATCH_POOL is used for temporary allocations.
 *
 * This function may be a no-op if the header cache lookup fails / misses.
 */
static svn_error_t *
prefetch_l2p_pages(svn_boolean_t *end,
                   svn_fs_t *fs,
                   svn_fs_fs__revision_file_t *rev_file,
                   svn_revnum_t first_revision,
                   svn_revnum_t revision,
                   apr_array_header_t *pages,
                   int exlcuded_page_no,
                   apr_off_t min_offset,
                   apr_off_t max_offset,
                   apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  int i;
  apr_pool_t *iterpool;
  svn_fs_fs__page_cache_key_t key = { 0 };

  /* Parameter check. */
  if (min_offset < 0)
    min_offset = 0;

  if (max_offset <= 0)
    {
      /* Nothing to do */
      *end = TRUE;
      return SVN_NO_ERROR;
    }

  /* get the page table for REVISION from cache */
  *end = FALSE;
  SVN_ERR(get_l2p_page_table(pages, fs, rev_file, revision, scratch_pool));
  if (pages->nelts == 0 || rev_file->l2p_stream == NULL)
    {
      /* not found -> we can't continue without hitting the disk again */
      *end = TRUE;
      return SVN_NO_ERROR;
    }

  /* prefetch pages individually until all are done or we found one in
   * the cache */
  iterpool = svn_pool_create(scratch_pool);
  assert(revision <= APR_UINT32_MAX);
  key.revision = (apr_uint32_t)revision;
  key.is_packed = rev_file->is_packed;

  for (i = 0; i < pages->nelts && !*end; ++i)
    {
      svn_boolean_t is_cached;

      l2p_page_table_entry_t *entry
        = &APR_ARRAY_IDX(pages, i, l2p_page_table_entry_t);
      svn_pool_clear(iterpool);

      if (i == exlcuded_page_no)
        continue;

      /* skip pages outside the specified index file range */
      if (   entry->offset < (apr_uint64_t)min_offset
          || entry->offset + entry->size > (apr_uint64_t)max_offset)
        {
          *end = TRUE;
          continue;
        }

      /* page already in cache? */
      key.page = i;
      SVN_ERR(svn_cache__has_key(&is_cached, ffd->l2p_page_cache,
                                 &key, iterpool));
      if (!is_cached)
        {
          /* no in cache -> read from stream (data already buffered in APR)
           * and cache the result */
          l2p_page_t *page = NULL;
          SVN_ERR(get_l2p_page(&page, rev_file, fs, first_revision, entry,
                               iterpool));

          SVN_ERR(svn_cache__set(ffd->l2p_page_cache, &key, page,
                                 iterpool));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Request data structure for l2p_entry_access_func.
 */
typedef struct l2p_entry_baton_t
{
  /* in data */
  /* revision. Used for error messages only */
  svn_revnum_t revision;

  /* item index to look up. Used for error messages only */
  apr_uint64_t item_index;

  /* offset within the cached page */
  apr_uint32_t page_offset;

  /* out data */
  /* absolute item or container offset in rev / pack file */
  apr_uint64_t offset;
} l2p_entry_baton_t;

/* Return the rev / pack file offset of the item at BATON->PAGE_OFFSET in
 * OFFSETS of PAGE and write it to *OFFSET.
 */
static svn_error_t *
l2p_page_get_entry(l2p_entry_baton_t *baton,
                   const l2p_page_t *page,
                   const apr_uint64_t *offsets,
                   apr_pool_t *scratch_pool)
{
  /* overflow check */
  if (page->entry_count <= baton->page_offset)
    return svn_error_createf(SVN_ERR_FS_INDEX_OVERFLOW , NULL,
                             _("Item index %s"
                               " too large in revision %ld"),
                             apr_psprintf(scratch_pool, "%" APR_UINT64_T_FMT,
                                          baton->item_index),
                             baton->revision);

  /* return the result */
  baton->offset = offsets[baton->page_offset];

  return SVN_NO_ERROR;
}

/* Implement svn_cache__partial_getter_func_t: copy the data requested in
 * l2p_entry_baton_t *BATON from l2p_page_t *DATA into BATON->OFFSET.
 * *OUT remains unchanged.
 */
static svn_error_t *
l2p_entry_access_func(void **out,
                      const void *data,
                      apr_size_t data_len,
                      void *baton,
                      apr_pool_t *result_pool)
{
  /* resolve all in-cache pointers */
  const l2p_page_t *page = data;
  const apr_uint64_t *offsets
    = svn_temp_deserializer__ptr(page, (const void *const *)&page->offsets);

  /* return the requested data */
  return l2p_page_get_entry(baton, page, offsets, result_pool);
}

/* Using the log-to-phys indexes in FS, find the absolute offset in the
 * rev file for (REVISION, ITEM_INDEX) and return it in *OFFSET.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
l2p_index_lookup(apr_off_t *offset,
                 svn_fs_t *fs,
                 svn_fs_fs__revision_file_t *rev_file,
                 svn_revnum_t revision,
                 apr_uint64_t item_index,
                 apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  l2p_page_info_baton_t info_baton;
  l2p_entry_baton_t page_baton;
  l2p_page_t *page = NULL;
  svn_fs_fs__page_cache_key_t key = { 0 };
  svn_boolean_t is_cached = FALSE;
  void *dummy = NULL;

  /* read index master data structure and extract the info required to
   * access the l2p index page for (REVISION,ITEM_INDEX)*/
  info_baton.revision = revision;
  info_baton.item_index = item_index;
  SVN_ERR(get_l2p_page_info(&info_baton, rev_file, fs, scratch_pool));

  /* try to find the page in the cache and get the OFFSET from it */
  page_baton.revision = revision;
  page_baton.item_index = item_index;
  page_baton.page_offset = info_baton.page_offset;

  assert(revision <= APR_UINT32_MAX);
  key.revision = (apr_uint32_t)revision;
  key.is_packed = svn_fs_fs__is_packed_rev(fs, revision);
  key.page = info_baton.page_no;

  SVN_ERR(svn_cache__get_partial(&dummy, &is_cached,
                                 ffd->l2p_page_cache, &key,
                                 l2p_entry_access_func, &page_baton,
                                 scratch_pool));

  if (!is_cached)
    {
      /* we need to read the info from disk (might already be in the
       * APR file buffer, though) */
      apr_array_header_t *pages;
      svn_revnum_t prefetch_revision;
      svn_revnum_t last_revision
        = info_baton.first_revision
          + (key.is_packed ? ffd->max_files_per_dir : 1);
      svn_boolean_t end;
      apr_off_t max_offset
        = APR_ALIGN(info_baton.entry.offset + info_baton.entry.size,
                    ffd->block_size);
      apr_off_t min_offset = max_offset - ffd->block_size;

      /* read the relevant page */
      SVN_ERR(get_l2p_page(&page, rev_file, fs, info_baton.first_revision,
                           &info_baton.entry, scratch_pool));

      /* cache the page and extract the result we need */
      SVN_ERR(svn_cache__set(ffd->l2p_page_cache, &key, page, scratch_pool));
      SVN_ERR(l2p_page_get_entry(&page_baton, page, page->offsets,
                                 scratch_pool));

      if (ffd->use_block_read)
        {
          apr_pool_t *iterpool = svn_pool_create(scratch_pool);

          /* prefetch pages from following and preceding revisions */
          pages = apr_array_make(scratch_pool, 16,
                                 sizeof(l2p_page_table_entry_t));
          end = FALSE;
          for (prefetch_revision = revision;
              prefetch_revision < last_revision && !end;
              ++prefetch_revision)
            {
              int excluded_page_no = prefetch_revision == revision
                                  ? info_baton.page_no
                                  : -1;
              svn_pool_clear(iterpool);

              SVN_ERR(prefetch_l2p_pages(&end, fs, rev_file,
                                        info_baton.first_revision,
                                        prefetch_revision, pages,
                                        excluded_page_no, min_offset,
                                        max_offset, iterpool));
            }

          end = FALSE;
          for (prefetch_revision = revision-1;
              prefetch_revision >= info_baton.first_revision && !end;
              --prefetch_revision)
            {
              svn_pool_clear(iterpool);

              SVN_ERR(prefetch_l2p_pages(&end, fs, rev_file,
                                        info_baton.first_revision,
                                        prefetch_revision, pages, -1,
                                        min_offset, max_offset, iterpool));
            }

          svn_pool_destroy(iterpool);
        }
    }

  *offset = page_baton.offset;

  return SVN_NO_ERROR;
}

/* Using the log-to-phys proto index in transaction TXN_ID in FS, find the
 * absolute offset in the proto rev file for the given ITEM_INDEX and return
 * it in *OFFSET.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
l2p_proto_index_lookup(apr_off_t *offset,
                       svn_fs_t *fs,
                       const svn_fs_fs__id_part_t *txn_id,
                       apr_uint64_t item_index,
                       apr_pool_t *scratch_pool)
{
  svn_boolean_t eof = FALSE;
  apr_file_t *file = NULL;
  SVN_ERR(svn_io_file_open(&file,
                           svn_fs_fs__path_l2p_proto_index(fs, txn_id,
                                                           scratch_pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT,
                           scratch_pool));

  /* process all entries until we fail due to EOF */
  *offset = -1;
  while (!eof)
    {
      l2p_proto_entry_t entry;

      /* (attempt to) read the next entry from the source */
      SVN_ERR(read_l2p_entry_from_proto_index(file, &entry, &eof,
                                              scratch_pool));

      /* handle new revision */
      if (!eof && entry.item_index == item_index)
        {
          *offset = (apr_off_t)entry.offset - 1;
          break;
        }
    }

  SVN_ERR(svn_io_file_close(file, scratch_pool));

  return SVN_NO_ERROR;
}

/* Read the log-to-phys header info of the index covering REVISION from FS
 * and return it in *HEADER.  REV_FILE provides the pack / rev status.
 * Allocate *HEADER in RESULT_POOL, use SCRATCH_POOL for temporary
 * allocations.
 */
static svn_error_t *
get_l2p_header(l2p_header_t **header,
               svn_fs_fs__revision_file_t *rev_file,
               svn_fs_t *fs,
               svn_revnum_t revision,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_boolean_t is_cached = FALSE;

  /* first, try cache lookop */
  pair_cache_key_t key;
  key.revision = rev_file->start_revision;
  key.second = rev_file->is_packed;
  SVN_ERR(svn_cache__get((void**)header, &is_cached, ffd->l2p_header_cache,
                         &key, result_pool));
  if (is_cached)
    return SVN_NO_ERROR;

  /* read from disk and cache the result */
  SVN_ERR(get_l2p_header_body(header, rev_file, fs, revision, result_pool,
                              scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__l2p_get_max_ids(apr_array_header_t **max_ids,
                           svn_fs_t *fs,
                           svn_revnum_t start_rev,
                           apr_size_t count,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  l2p_header_t *header = NULL;
  svn_revnum_t revision;
  svn_revnum_t last_rev = (svn_revnum_t)(start_rev + count);
  svn_fs_fs__revision_file_t *rev_file;
  apr_pool_t *header_pool = svn_pool_create(scratch_pool);

  /* read index master data structure for the index covering START_REV */
  SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&rev_file, fs, start_rev,
                                           header_pool, header_pool));
  SVN_ERR(get_l2p_header(&header, rev_file, fs, start_rev, header_pool,
                         header_pool));
  SVN_ERR(svn_fs_fs__close_revision_file(rev_file));

  /* Determine the length of the item index list for each rev.
   * Read new index headers as required. */
  *max_ids = apr_array_make(result_pool, (int)count, sizeof(apr_uint64_t));
  for (revision = start_rev; revision < last_rev; ++revision)
    {
      apr_uint64_t full_page_count;
      apr_uint64_t item_count;
      apr_size_t first_page_index, last_page_index;

      if (revision - header->first_revision >= header->revision_count)
        {
          /* need to read the next index. Clear up memory used for the
           * previous one.  Note that intermittent pack runs do not change
           * the number of items in a revision, i.e. there is no consistency
           * issue here. */
          svn_pool_clear(header_pool);
          SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&rev_file, fs, revision,
                                                  header_pool, header_pool));
          SVN_ERR(get_l2p_header(&header, rev_file, fs, revision,
                                 header_pool, header_pool));
          SVN_ERR(svn_fs_fs__close_revision_file(rev_file));
        }

      /* in a revision with N index pages, the first N-1 index pages are
       * "full", i.e. contain HEADER->PAGE_SIZE entries */
      first_page_index
         = header->page_table_index[revision - header->first_revision];
      last_page_index
         = header->page_table_index[revision - header->first_revision + 1];
      full_page_count = last_page_index - first_page_index - 1;
      item_count = full_page_count * header->page_size
                 + header->page_table[last_page_index - 1].entry_count;

      APR_ARRAY_PUSH(*max_ids, apr_uint64_t) = item_count;
    }

  svn_pool_destroy(header_pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__item_offset(apr_off_t *absolute_position,
                       svn_fs_t *fs,
                       svn_fs_fs__revision_file_t *rev_file,
                       svn_revnum_t revision,
                       const svn_fs_fs__id_part_t *txn_id,
                       apr_uint64_t item_index,
                       apr_pool_t *scratch_pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  if (txn_id)
    {
      if (svn_fs_fs__use_log_addressing(fs))
        {
          /* the txn is going to produce a rev with logical addressing.
             So, we need to get our info from the (proto) index file. */
          SVN_ERR(l2p_proto_index_lookup(absolute_position, fs, txn_id,
                                         item_index, scratch_pool));
        }
      else
        {
          /* for data in txns, item_index *is* the offset */
          *absolute_position = item_index;
        }
    }
  else if (svn_fs_fs__use_log_addressing(fs))
    {
      /* ordinary index lookup */
      SVN_ERR(l2p_index_lookup(absolute_position, fs, rev_file, revision,
                               item_index, scratch_pool));
    }
  else if (rev_file->is_packed)
    {
      /* pack file with physical addressing */
      apr_off_t rev_offset;
      SVN_ERR(svn_fs_fs__get_packed_offset(&rev_offset, fs, revision,
                                           scratch_pool));
      *absolute_position = rev_offset + item_index;
    }
  else
    {
      /* for non-packed revs with physical addressing,
         item_index *is* the offset */
      *absolute_position = item_index;
    }

  return svn_error_trace(err);
}

/*
 * phys-to-log index
 */
svn_error_t *
svn_fs_fs__p2l_proto_index_open(apr_file_t **proto_index,
                                const char *file_name,
                                apr_pool_t *result_pool)
{
  SVN_ERR(svn_io_file_open(proto_index, file_name, APR_READ | APR_WRITE
                           | APR_CREATE | APR_APPEND | APR_BUFFERED,
                           APR_OS_DEFAULT, result_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__p2l_proto_index_add_entry(apr_file_t *proto_index,
                                     const svn_fs_fs__p2l_entry_t *entry,
                                     apr_pool_t *scratch_pool)
{
  apr_uint64_t revision;

  /* Make sure all signed elements of ENTRY have non-negative values.
   *
   * For file offsets and sizes, this is a given as we use them to describe
   * absolute positions and sizes.  For revisions, SVN_INVALID_REVNUM is
   * valid, hence we have to shift it by 1.
   */
  SVN_ERR_ASSERT(entry->offset >= 0);
  SVN_ERR_ASSERT(entry->size >= 0);
  SVN_ERR_ASSERT(   entry->item.revision >= 0
                 || entry->item.revision == SVN_INVALID_REVNUM);

  revision = entry->item.revision == SVN_INVALID_REVNUM
           ? 0
           : ((apr_uint64_t)entry->item.revision + 1);

  /* Now, all values will nicely convert to uint64. */
  /* Make sure to keep P2L_PROTO_INDEX_ENTRY_SIZE consistent with this: */

  SVN_ERR(write_uint64_to_proto_index(proto_index, entry->offset,
                                      scratch_pool));
  SVN_ERR(write_uint64_to_proto_index(proto_index, entry->size,
                                      scratch_pool));
  SVN_ERR(write_uint64_to_proto_index(proto_index, entry->type,
                                      scratch_pool));
  SVN_ERR(write_uint64_to_proto_index(proto_index, entry->fnv1_checksum,
                                      scratch_pool));
  SVN_ERR(write_uint64_to_proto_index(proto_index, revision,
                                      scratch_pool));
  SVN_ERR(write_uint64_to_proto_index(proto_index, entry->item.number,
                                      scratch_pool));

  return SVN_NO_ERROR;
}

/* Read *ENTRY from log-to-phys PROTO_INDEX file and indicate end-of-file
 * in *EOF, or error out in that case if EOF is NULL.  *ENTRY is in an
 * undefined state if an end-of-file occurred.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
read_p2l_entry_from_proto_index(apr_file_t *proto_index,
                                svn_fs_fs__p2l_entry_t *entry,
                                svn_boolean_t *eof,
                                apr_pool_t *scratch_pool)
{
  apr_uint64_t revision;

  SVN_ERR(read_off_t_from_proto_index(proto_index, &entry->offset,
                                      eof, scratch_pool));
  SVN_ERR(read_off_t_from_proto_index(proto_index, &entry->size,
                                      eof, scratch_pool));
  SVN_ERR(read_uint32_from_proto_index(proto_index, &entry->type,
                                       eof, scratch_pool));
  SVN_ERR(read_uint32_from_proto_index(proto_index, &entry->fnv1_checksum,
                                       eof, scratch_pool));
  SVN_ERR(read_uint64_from_proto_index(proto_index, &revision,
                                       eof, scratch_pool));
  SVN_ERR(read_uint64_from_proto_index(proto_index, &entry->item.number,
                                       eof, scratch_pool));

  /* Do the inverse REVSION number conversion (see
   * svn_fs_fs__p2l_proto_index_add_entry), if we actually read a complete
   * record.
   */
  if (!eof || !*eof)
    {
      /* Be careful with the arithmetics here (overflows and wrap-around): */
      if (revision > 0 && revision - 1 > LONG_MAX)
        return svn_error_createf(SVN_ERR_FS_INDEX_OVERFLOW, NULL,
                                _("Revision 0x%s too large, max = 0x%s"),
                                apr_psprintf(scratch_pool,
                                             "%" APR_UINT64_T_HEX_FMT,
                                             revision),
                                apr_psprintf(scratch_pool,
                                             "%" APR_UINT64_T_HEX_FMT,
                                             (apr_uint64_t)LONG_MAX));

      /* Shortening conversion from unsigned to signed int is well-defined
       * and not lossy in C because the value can be represented in the
       * target type.  Also, cast to 'long' instead of 'svn_revnum_t' here
       * to provoke a compiler warning if those types should differ and we
       * would need to change the overflow checking logic.
       */
      entry->item.revision = revision == 0
                           ? SVN_INVALID_REVNUM
                           : (long)(revision - 1);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__p2l_proto_index_next_offset(apr_off_t *next_offset,
                                       apr_file_t *proto_index,
                                       apr_pool_t *scratch_pool)
{
  apr_off_t offset = 0;

  /* Empty index file? */
  SVN_ERR(svn_io_file_seek(proto_index, APR_END, &offset, scratch_pool));
  if (offset == 0)
    {
      *next_offset = 0;
    }
  else
    {
      /* At least one entry.  Read last entry. */
      svn_fs_fs__p2l_entry_t entry;
      offset -= P2L_PROTO_INDEX_ENTRY_SIZE;

      SVN_ERR(svn_io_file_seek(proto_index, APR_SET, &offset, scratch_pool));
      SVN_ERR(read_p2l_entry_from_proto_index(proto_index, &entry,
                                              NULL, scratch_pool));

      /* Return next offset. */
      *next_offset = entry.offset + entry.size;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__p2l_index_append(svn_checksum_t **checksum,
                            svn_fs_t *fs,
                            apr_file_t *index_file,
                            const char *proto_file_name,
                            svn_revnum_t revision,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_uint64_t page_size = ffd->p2l_page_size;
  apr_file_t *proto_index = NULL;
  svn_stream_t *stream;
  int i;
  svn_boolean_t eof = FALSE;
  unsigned char encoded[ENCODED_INT_LENGTH];
  svn_revnum_t last_revision = revision;
  apr_uint64_t last_compound = 0;

  apr_uint64_t last_entry_end = 0;
  apr_uint64_t last_page_end = 0;
  apr_uint64_t last_buffer_size = 0;  /* byte offset in the spill buffer at
                                         the begin of the current revision */
  apr_uint64_t file_size = 0;

  /* temporary data structures that collect the data which will be moved
     to the target file in a second step */
  apr_pool_t *local_pool = svn_pool_create(scratch_pool);
  apr_array_header_t *table_sizes
     = apr_array_make(local_pool, 16, sizeof(apr_uint64_t));

  /* 64k blocks, spill after 16MB */
  svn_spillbuf_t *buffer
     = svn_spillbuf__create(0x10000, 0x1000000, local_pool);

  /* for loop temps ... */
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* start at the beginning of the source file */
  SVN_ERR(svn_io_file_open(&proto_index, proto_file_name,
                           APR_READ | APR_CREATE | APR_BUFFERED,
                           APR_OS_DEFAULT, scratch_pool));

  /* process all entries until we fail due to EOF */
  while (!eof)
    {
      svn_fs_fs__p2l_entry_t entry;
      apr_uint64_t entry_end;
      svn_boolean_t new_page = svn_spillbuf__get_size(buffer) == 0;
      apr_uint64_t compound;
      apr_int64_t rev_diff, compound_diff;

      svn_pool_clear(iterpool);

      /* (attempt to) read the next entry from the source */
      SVN_ERR(read_p2l_entry_from_proto_index(proto_index, &entry,
                                              &eof, iterpool));

      /* "unused" (and usually non-existent) section to cover the offsets
         at the end the of the last page. */
      if (eof)
        {
          file_size = last_entry_end;

          entry.offset = last_entry_end;
          entry.size = APR_ALIGN(entry.offset, page_size) - entry.offset;
          entry.type = SVN_FS_FS__ITEM_TYPE_UNUSED;
          entry.fnv1_checksum = 0;
          entry.item.revision = last_revision;
          entry.item.number = 0;
        }
      else
        {
          /* fix-up items created when the txn's target rev was unknown */
          if (entry.item.revision == SVN_INVALID_REVNUM)
            entry.item.revision = revision;
        }

      /* end pages if entry is extending beyond their boundaries */
      entry_end = entry.offset + entry.size;
      while (entry_end - last_page_end > page_size)
        {
          apr_uint64_t buffer_size = svn_spillbuf__get_size(buffer);
          APR_ARRAY_PUSH(table_sizes, apr_uint64_t)
             = buffer_size - last_buffer_size;

          last_buffer_size = buffer_size;
          last_page_end += page_size;
          new_page = TRUE;
        }

      /* this entry starts a new table -> store its offset
         (all following entries in the same table will store sizes only) */
      if (new_page)
        {
          SVN_ERR(svn_spillbuf__write(buffer, (const char *)encoded,
                                      encode_uint(encoded, entry.offset),
                                      iterpool));
          last_revision = revision;
          last_compound = 0;
        }

      /* write simple item entry */
      SVN_ERR(svn_spillbuf__write(buffer, (const char *)encoded,
                                  encode_uint(encoded, entry.size),
                                  iterpool));

      rev_diff = entry.item.revision - last_revision;
      last_revision = entry.item.revision;

      compound = entry.item.number * 8 + entry.type;
      compound_diff = compound - last_compound;
      last_compound = compound;

      SVN_ERR(svn_spillbuf__write(buffer, (const char *)encoded,
                                  encode_int(encoded, compound_diff),
                                  iterpool));
      SVN_ERR(svn_spillbuf__write(buffer, (const char *)encoded,
                                  encode_int(encoded, rev_diff),
                                  iterpool));
      SVN_ERR(svn_spillbuf__write(buffer, (const char *)encoded,
                                  encode_uint(encoded, entry.fnv1_checksum),
                                  iterpool));

      last_entry_end = entry_end;
    }

  /* close the source file */
  SVN_ERR(svn_io_file_close(proto_index, local_pool));

  /* store length of last table */
  APR_ARRAY_PUSH(table_sizes, apr_uint64_t)
      = svn_spillbuf__get_size(buffer) - last_buffer_size;

  /* Open target stream. */
  stream = svn_stream_checksummed2(svn_stream_from_aprfile2(index_file, TRUE,
                                                            local_pool),
                                   NULL, checksum, svn_checksum_md5, FALSE,
                                   result_pool);

  /* write the start revision, file size and page size */
  SVN_ERR(svn_stream_puts(stream, P2L_STREAM_PREFIX));
  SVN_ERR(stream_write_encoded(stream, revision));
  SVN_ERR(stream_write_encoded(stream, file_size));
  SVN_ERR(stream_write_encoded(stream, page_size));

  /* write the page table (actually, the sizes of each page description) */
  SVN_ERR(stream_write_encoded(stream, table_sizes->nelts));
  for (i = 0; i < table_sizes->nelts; ++i)
    {
      apr_uint64_t value = APR_ARRAY_IDX(table_sizes, i, apr_uint64_t);
      SVN_ERR(stream_write_encoded(stream, value));
    }

  /* append page contents and implicitly close STREAM */
  SVN_ERR(svn_stream_copy3(svn_stream__from_spillbuf(buffer, local_pool),
                           stream, NULL, NULL, local_pool));

  svn_pool_destroy(iterpool);
  svn_pool_destroy(local_pool);

  return SVN_NO_ERROR;
}

/* If REV_FILE->P2L_STREAM is NULL, create a new stream for the phys-to-log
 * index for REVISION in FS using the rev / pack file provided by REV_FILE.
 */
static svn_error_t *
auto_open_p2l_index(svn_fs_fs__revision_file_t *rev_file,
                    svn_fs_t *fs,
                    svn_revnum_t revision)
{
  if (rev_file->p2l_stream == NULL)
    {
      fs_fs_data_t *ffd = fs->fsap_data;

      SVN_ERR(svn_fs_fs__auto_read_footer(rev_file));
      SVN_ERR(packed_stream_open(&rev_file->p2l_stream,
                                 rev_file->file,
                                 rev_file->p2l_offset,
                                 rev_file->footer_offset,
                                 P2L_STREAM_PREFIX,
                                 (apr_size_t)ffd->block_size,
                                 rev_file->pool,
                                 rev_file->pool));
    }

  return SVN_NO_ERROR;
}


/* Read the header data structure of the phys-to-log index for REVISION in
 * FS and return it in *HEADER, allocated in RESULT_POOL. Use REV_FILE to
 * access on-disk data.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
get_p2l_header(p2l_header_t **header,
               svn_fs_fs__revision_file_t *rev_file,
               svn_fs_t *fs,
               svn_revnum_t revision,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_uint64_t value;
  apr_size_t i;
  apr_off_t offset;
  p2l_header_t *result;
  svn_boolean_t is_cached = FALSE;

  /* look for the header data in our cache */
  pair_cache_key_t key;
  key.revision = rev_file->start_revision;
  key.second = rev_file->is_packed;

  SVN_ERR(svn_cache__get((void**)header, &is_cached, ffd->p2l_header_cache,
                         &key, result_pool));
  if (is_cached)
    return SVN_NO_ERROR;

  /* not found -> must read it from disk.
   * Open index file or position read pointer to the begin of the file */
  if (rev_file->p2l_stream == NULL)
    SVN_ERR(auto_open_p2l_index(rev_file, fs, rev_file->start_revision));
  else
    packed_stream_seek(rev_file->p2l_stream, 0);

  /* allocate result data structure */
  result = apr_pcalloc(result_pool, sizeof(*result));

  /* Read table sizes, check them for plausibility and allocate page array. */
  SVN_ERR(packed_stream_get(&value, rev_file->p2l_stream));
  result->first_revision = (svn_revnum_t)value;
  if (result->first_revision != rev_file->start_revision)
    return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                  _("Index rev / pack file revision numbers do not match"));

  SVN_ERR(packed_stream_get(&value, rev_file->p2l_stream));
  result->file_size = value;
  if (result->file_size != (apr_uint64_t)rev_file->l2p_offset)
    return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                   _("Index offset and rev / pack file size do not match"));

  SVN_ERR(packed_stream_get(&value, rev_file->p2l_stream));
  result->page_size = value;
  if (!result->page_size || (result->page_size & (result->page_size - 1)))
    return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                            _("P2L index page size is not a power of two"));

  SVN_ERR(packed_stream_get(&value, rev_file->p2l_stream));
  result->page_count = (apr_size_t)value;
  if (result->page_count != (result->file_size - 1) / result->page_size + 1)
    return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                   _("P2L page count does not match rev / pack file size"));

  result->offsets
    = apr_pcalloc(result_pool, (result->page_count + 1) * sizeof(*result->offsets));

  /* read page sizes and derive page description offsets from them */
  result->offsets[0] = 0;
  for (i = 0; i < result->page_count; ++i)
    {
      SVN_ERR(packed_stream_get(&value, rev_file->p2l_stream));
      result->offsets[i+1] = result->offsets[i] + (apr_off_t)value;
    }

  /* correct the offset values */
  offset = packed_stream_offset(rev_file->p2l_stream);
  for (i = 0; i <= result->page_count; ++i)
    result->offsets[i] += offset;

  /* cache the header data */
  SVN_ERR(svn_cache__set(ffd->p2l_header_cache, &key, result, scratch_pool));

  /* return the result */
  *header = result;

  return SVN_NO_ERROR;
}

/* Data structure that describes which p2l page info shall be extracted
 * from the cache and contains the fields that receive the result.
 */
typedef struct p2l_page_info_baton_t
{
  /* input variables */
  /* revision identifying the index file */
  svn_revnum_t revision;

  /* offset within the page in rev / pack file */
  apr_off_t offset;

  /* output variables */
  /* page containing OFFSET */
  apr_size_t page_no;

  /* first revision in this p2l index */
  svn_revnum_t first_revision;

  /* offset within the p2l index file describing this page */
  apr_off_t start_offset;

  /* offset within the p2l index file describing the following page */
  apr_off_t next_offset;

  /* PAGE_NO * PAGE_SIZE (if <= OFFSET) */
  apr_off_t page_start;

  /* total number of pages indexed */
  apr_size_t page_count;

  /* size of each page in pack / rev file */
  apr_uint64_t page_size;
} p2l_page_info_baton_t;

/* From HEADER and the list of all OFFSETS, fill BATON with the page info
 * requested by BATON->OFFSET.
 */
static void
p2l_page_info_copy(p2l_page_info_baton_t *baton,
                   const p2l_header_t *header,
                   const apr_off_t *offsets)
{
  /* if the requested offset is out of bounds, return info for
   * a zero-sized empty page right behind the last page.
   */
  if (baton->offset / header->page_size < header->page_count)
    {
      /* This cast is safe because the value is < header->page_count. */
      baton->page_no = (apr_size_t)(baton->offset / header->page_size);
      baton->start_offset = offsets[baton->page_no];
      baton->next_offset = offsets[baton->page_no + 1];
      baton->page_size = header->page_size;
    }
  else
    {
      /* Beyond the last page. */
      baton->page_no = header->page_count;
      baton->start_offset = offsets[baton->page_no];
      baton->next_offset = offsets[baton->page_no];
      baton->page_size = 0;
    }

  baton->first_revision = header->first_revision;
  baton->page_start = (apr_off_t)(header->page_size * baton->page_no);
  baton->page_count = header->page_count;
}

/* Implement svn_cache__partial_getter_func_t: extract the p2l page info
 * requested by BATON and return it in BATON.
 */
static svn_error_t *
p2l_page_info_func(void **out,
                   const void *data,
                   apr_size_t data_len,
                   void *baton,
                   apr_pool_t *result_pool)
{
  /* all the pointers to cached data we need */
  const p2l_header_t *header = data;
  const apr_off_t *offsets
    = svn_temp_deserializer__ptr(header,
                                 (const void *const *)&header->offsets);

  /* copy data from cache to BATON */
  p2l_page_info_copy(baton, header, offsets);
  return SVN_NO_ERROR;
}

/* Read the header data structure of the phys-to-log index for revision
 * BATON->REVISION in FS.  Return in *BATON all info relevant to read the
 * index page for the rev / pack file offset BATON->OFFSET.  Use REV_FILE
 * to access on-disk data.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
get_p2l_page_info(p2l_page_info_baton_t *baton,
                  svn_fs_fs__revision_file_t *rev_file,
                  svn_fs_t *fs,
                  apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  p2l_header_t *header;
  svn_boolean_t is_cached = FALSE;
  void *dummy = NULL;

  /* look for the header data in our cache */
  pair_cache_key_t key;
  key.revision = rev_file->start_revision;
  key.second = rev_file->is_packed;

  SVN_ERR(svn_cache__get_partial(&dummy, &is_cached, ffd->p2l_header_cache,
                                 &key, p2l_page_info_func, baton,
                                 scratch_pool));
  if (is_cached)
    return SVN_NO_ERROR;

  SVN_ERR(get_p2l_header(&header, rev_file, fs, baton->revision,
                         scratch_pool, scratch_pool));

  /* copy the requested info into *BATON */
  p2l_page_info_copy(baton, header, header->offsets);

  return SVN_NO_ERROR;
}

/* Read a mapping entry from the phys-to-log index STREAM and append it to
 * RESULT.  *ITEM_INDEX contains the phys offset for the entry and will
 * be moved forward by the size of entry.
 */
static svn_error_t *
read_entry(svn_fs_fs__packed_number_stream_t *stream,
           apr_off_t *item_offset,
           svn_revnum_t *last_revision,
           apr_uint64_t *last_compound,
           apr_array_header_t *result)
{
  apr_uint64_t value;

  svn_fs_fs__p2l_entry_t entry;

  entry.offset = *item_offset;
  SVN_ERR(packed_stream_get(&value, stream));
  entry.size = (apr_off_t)value;

  SVN_ERR(packed_stream_get(&value, stream));
  *last_compound += decode_int(value);

  entry.type = *last_compound & 7;
  entry.item.number = *last_compound / 8;

  /* Verify item type. */
  if (entry.type > SVN_FS_FS__ITEM_TYPE_CHANGES)
    return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                            _("Invalid item type in P2L index"));
  if (   entry.type == SVN_FS_FS__ITEM_TYPE_CHANGES
      && entry.item.number != SVN_FS_FS__ITEM_INDEX_CHANGES)
    return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                            _("Changed path list must have item number 1"));

  SVN_ERR(packed_stream_get(&value, stream));
  *last_revision += (svn_revnum_t)decode_int(value);
  entry.item.revision = *last_revision;

  SVN_ERR(packed_stream_get(&value, stream));
  entry.fnv1_checksum = (apr_uint32_t)value;

  /* Truncating the checksum to 32 bits may have hidden random data in the
   * unused extra bits of the on-disk representation (7/8 bit representation
   * uses 5 bytes on disk for the 32 bit value, leaving 3 bits unused). */
  if (value > APR_UINT32_MAX)
    return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                            _("Invalid FNV1 checksum in P2L index"));

  /* Some of the index data for empty rev / pack file sections will not be
   * used during normal operation.  Thus, we have strict rules for the
   * contents of those unused fields. */
  if (entry.type == SVN_FS_FS__ITEM_TYPE_UNUSED)
    if (   entry.item.number != SVN_FS_FS__ITEM_INDEX_UNUSED
        || entry.fnv1_checksum != 0)
      return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
                 _("Empty regions must have item number 0 and checksum 0"));

  /* Corrupted SIZE values might cause arithmetic overflow.
   * The same can happen if you copy a repository from a system with 63 bit
   * file lengths to one with 31 bit file lengths. */
  if ((apr_uint64_t)entry.offset + (apr_uint64_t)entry.size > off_t_max)
    return svn_error_create(SVN_ERR_FS_INDEX_OVERFLOW , NULL,
                            _("P2L index entry size overflow."));

  APR_ARRAY_PUSH(result, svn_fs_fs__p2l_entry_t) = entry;
  *item_offset += entry.size;

  return SVN_NO_ERROR;
}

/* Read the phys-to-log mappings for the cluster beginning at rev file
 * offset PAGE_START from the index for START_REVISION in FS.  The data
 * can be found in the index page beginning at START_OFFSET with the next
 * page beginning at NEXT_OFFSET.  PAGE_SIZE is the L2P index page size.
 * Return the relevant index entries in *ENTRIES.  Use REV_FILE to access
 * on-disk data.  Allocate *ENTRIES in RESULT_POOL.
 */
static svn_error_t *
get_p2l_page(apr_array_header_t **entries,
             svn_fs_fs__revision_file_t *rev_file,
             svn_fs_t *fs,
             svn_revnum_t start_revision,
             apr_off_t start_offset,
             apr_off_t next_offset,
             apr_off_t page_start,
             apr_uint64_t page_size,
             apr_pool_t *result_pool)
{
  apr_uint64_t value;
  apr_array_header_t *result
    = apr_array_make(result_pool, 16, sizeof(svn_fs_fs__p2l_entry_t));
  apr_off_t item_offset;
  apr_off_t offset;
  svn_revnum_t last_revision;
  apr_uint64_t last_compound;

  /* open index and navigate to page start */
  SVN_ERR(auto_open_p2l_index(rev_file, fs, start_revision));
  packed_stream_seek(rev_file->p2l_stream, start_offset);

  /* read rev file offset of the first page entry (all page entries will
   * only store their sizes). */
  SVN_ERR(packed_stream_get(&value, rev_file->p2l_stream));
  item_offset = (apr_off_t)value;

  /* read all entries of this page */
  last_revision = start_revision;
  last_compound = 0;

  /* Special case: empty pages. */
  if (start_offset == next_offset)
    {
      /* Empty page. This only happens if the first entry of the next page
       * also covers this page (and possibly more) completely. */
      SVN_ERR(read_entry(rev_file->p2l_stream, &item_offset,
                         &last_revision, &last_compound, result));
    }
  else
    {
      /* Read non-empty page. */
      do
        {
          SVN_ERR(read_entry(rev_file->p2l_stream, &item_offset,
                             &last_revision, &last_compound, result));
          offset = packed_stream_offset(rev_file->p2l_stream);
        }
      while (offset < next_offset);

      /* We should now be exactly at the next offset, i.e. the numbers in
       * the stream cannot overlap into the next page description. */
      if (offset != next_offset)
        return svn_error_create(SVN_ERR_FS_INDEX_CORRUPTION, NULL,
             _("P2L page description overlaps with next page description"));

      /* if we haven't covered the cluster end yet, we must read the first
       * entry of the next page */
      if (item_offset < page_start + page_size)
        {
          SVN_ERR(packed_stream_get(&value, rev_file->p2l_stream));
          item_offset = (apr_off_t)value;
          last_revision = start_revision;
          last_compound = 0;
          SVN_ERR(read_entry(rev_file->p2l_stream, &item_offset,
                             &last_revision, &last_compound, result));
        }
    }

  *entries = result;

  return SVN_NO_ERROR;
}

/* If it cannot be found in FS's caches, read the p2l index page selected
 * by BATON->OFFSET from REV_FILE.  Don't read the page if it precedes
 * MIN_OFFSET.  Set *END to TRUE if the caller should stop refeching.
 *
 * *BATON will be updated with the selected page's info and SCRATCH_POOL
 * will be used for temporary allocations.  If the data is alread in the
 * cache, descrease *LEAKING_BUCKET and increase it otherwise.  With that
 * pattern we will still read all pages from the block even if some of
 * them survived in the cached.
 */
static svn_error_t *
prefetch_p2l_page(svn_boolean_t *end,
                  int *leaking_bucket,
                  svn_fs_t *fs,
                  svn_fs_fs__revision_file_t *rev_file,
                  p2l_page_info_baton_t *baton,
                  apr_off_t min_offset,
                  apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_boolean_t already_cached;
  apr_array_header_t *page;
  svn_fs_fs__page_cache_key_t key = { 0 };

  /* fetch the page info */
  *end = FALSE;
  baton->revision = baton->first_revision;
  SVN_ERR(get_p2l_page_info(baton, rev_file, fs, scratch_pool));
  if (baton->start_offset < min_offset || !rev_file->p2l_stream)
    {
      /* page outside limits -> stop prefetching */
      *end = TRUE;
      return SVN_NO_ERROR;
    }

  /* do we have that page in our caches already? */
  assert(baton->first_revision <= APR_UINT32_MAX);
  key.revision = (apr_uint32_t)baton->first_revision;
  key.is_packed = svn_fs_fs__is_packed_rev(fs, baton->first_revision);
  key.page = baton->page_no;
  SVN_ERR(svn_cache__has_key(&already_cached, ffd->p2l_page_cache,
                             &key, scratch_pool));

  /* yes, already cached */
  if (already_cached)
    {
      /* stop prefetching if most pages are already cached. */
      if (!--*leaking_bucket)
        *end = TRUE;

      return SVN_NO_ERROR;
    }

  ++*leaking_bucket;

  /* read from disk */
  SVN_ERR(get_p2l_page(&page, rev_file, fs,
                       baton->first_revision,
                       baton->start_offset,
                       baton->next_offset,
                       baton->page_start,
                       baton->page_size,
                       scratch_pool));

  /* and put it into our cache */
  SVN_ERR(svn_cache__set(ffd->p2l_page_cache, &key, page, scratch_pool));

  return SVN_NO_ERROR;
}

/* Lookup & construct the baton and key information that we will need for
 * a P2L page cache lookup.  We want the page covering OFFSET in the rev /
 * pack file containing REVSION in FS.  Return the results in *PAGE_INFO_P
 * and *KEY_P.  Read data through REV_FILE.  Use SCRATCH_POOL for temporary
 * allocations.
 */
static svn_error_t *
get_p2l_keys(p2l_page_info_baton_t *page_info_p,
             svn_fs_fs__page_cache_key_t *key_p,
             svn_fs_fs__revision_file_t *rev_file,
             svn_fs_t *fs,
             svn_revnum_t revision,
             apr_off_t offset,
             apr_pool_t *scratch_pool)
{
  p2l_page_info_baton_t page_info;

  /* request info for the index pages that describes the pack / rev file
   * contents at pack / rev file position OFFSET. */
  page_info.offset = offset;
  page_info.revision = revision;
  SVN_ERR(get_p2l_page_info(&page_info, rev_file, fs, scratch_pool));

  /* if the offset refers to a non-existent page, bail out */
  if (page_info.page_count <= page_info.page_no)
    return svn_error_createf(SVN_ERR_FS_INDEX_OVERFLOW , NULL,
                              _("Offset %s too large in revision %ld"),
                              apr_off_t_toa(scratch_pool, offset), revision);

  /* return results */
  if (page_info_p)
    *page_info_p = page_info;

  /* construct cache key */
  if (key_p)
    {
      svn_fs_fs__page_cache_key_t key = { 0 };
      assert(page_info.first_revision <= APR_UINT32_MAX);
      key.revision = (apr_uint32_t)page_info.first_revision;
      key.is_packed = rev_file->is_packed;
      key.page = page_info.page_no;

      *key_p = key;
    }

  return SVN_NO_ERROR;
}

/* qsort-compatible compare function that compares the OFFSET of the
 * svn_fs_fs__p2l_entry_t in *LHS with the apr_off_t in *RHS. */
static int
compare_start_p2l_entry(const void *lhs,
                        const void *rhs)
{
  const svn_fs_fs__p2l_entry_t *entry = lhs;
  apr_off_t start = *(const apr_off_t*)rhs;
  apr_off_t diff = entry->offset - start;

  /* restrict result to int */
  return diff < 0 ? -1 : (diff == 0 ? 0 : 1);
}

/* From the PAGE_ENTRIES array of svn_fs_fs__p2l_entry_t, ordered
 * by their OFFSET member, copy all elements overlapping the range
 * [BLOCK_START, BLOCK_END) to ENTRIES. */
static void
append_p2l_entries(apr_array_header_t *entries,
                   apr_array_header_t *page_entries,
                   apr_off_t block_start,
                   apr_off_t block_end)
{
  const svn_fs_fs__p2l_entry_t *entry;
  int idx = svn_sort__bsearch_lower_bound(page_entries, &block_start,
                                          compare_start_p2l_entry);

  /* start at the first entry that overlaps with BLOCK_START */
  if (idx > 0)
    {
      entry = &APR_ARRAY_IDX(page_entries, idx - 1, svn_fs_fs__p2l_entry_t);
      if (entry->offset + entry->size > block_start)
        --idx;
    }

  /* copy all entries covering the requested range */
  for ( ; idx < page_entries->nelts; ++idx)
    {
      entry = &APR_ARRAY_IDX(page_entries, idx, svn_fs_fs__p2l_entry_t);
      if (entry->offset >= block_end)
        break;

      APR_ARRAY_PUSH(entries, svn_fs_fs__p2l_entry_t) = *entry;
    }
}

/* Auxilliary struct passed to p2l_entries_func selecting the relevant
 * data range. */
typedef struct p2l_entries_baton_t
{
  apr_off_t start;
  apr_off_t end;
} p2l_entries_baton_t;

/* Implement svn_cache__partial_getter_func_t: extract p2l entries from
 * the page in DATA which overlap the p2l_entries_baton_t in BATON.
 * The target array is already provided in *OUT.
 */
static svn_error_t *
p2l_entries_func(void **out,
                 const void *data,
                 apr_size_t data_len,
                 void *baton,
                 apr_pool_t *result_pool)
{
  apr_array_header_t *entries = *(apr_array_header_t **)out;
  const apr_array_header_t *raw_page = data;
  p2l_entries_baton_t *block = baton;

  /* Make PAGE a readable APR array. */
  apr_array_header_t page = *raw_page;
  page.elts = (void *)svn_temp_deserializer__ptr(raw_page,
                                    (const void * const *)&raw_page->elts);

  /* append relevant information to result */
  append_p2l_entries(entries, &page, block->start, block->end);

  return SVN_NO_ERROR;
}


/* Body of svn_fs_fs__p2l_index_lookup.  However, do a single index page
 * lookup and append the result to the ENTRIES array provided by the caller.
 * Use successive calls to cover larger ranges.
 */
static svn_error_t *
p2l_index_lookup(apr_array_header_t *entries,
                 svn_fs_fs__revision_file_t *rev_file,
                 svn_fs_t *fs,
                 svn_revnum_t revision,
                 apr_off_t block_start,
                 apr_off_t block_end,
                 apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_fs_fs__page_cache_key_t key;
  svn_boolean_t is_cached = FALSE;
  p2l_page_info_baton_t page_info;
  apr_array_header_t *local_result = entries;

  /* baton selecting the relevant entries from the one page we access */
  p2l_entries_baton_t block;
  block.start = block_start;
  block.end = block_end;

  /* if we requested an empty range, the result would be empty */
  SVN_ERR_ASSERT(block_start < block_end);

  /* look for the fist page of the range in our cache */
  SVN_ERR(get_p2l_keys(&page_info, &key, rev_file, fs, revision, block_start,
                       scratch_pool));
  SVN_ERR(svn_cache__get_partial((void**)&local_result, &is_cached,
                                 ffd->p2l_page_cache, &key, p2l_entries_func,
                                 &block, scratch_pool));

  if (!is_cached)
    {
      svn_boolean_t end;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      apr_off_t original_page_start = page_info.page_start;
      int leaking_bucket = 4;
      p2l_page_info_baton_t prefetch_info = page_info;
      apr_array_header_t *page_entries;

      apr_off_t max_offset
        = APR_ALIGN(page_info.next_offset, ffd->block_size);
      apr_off_t min_offset
        = APR_ALIGN(page_info.start_offset, ffd->block_size) - ffd->block_size;

      /* Since we read index data in larger chunks, we probably got more
       * page data than we requested.  Parse & cache that until either we
       * encounter pages already cached or reach the end of the buffer.
       */

      /* pre-fetch preceding pages */
      if (ffd->use_block_read)
        {
          end = FALSE;
          prefetch_info.offset = original_page_start;
          while (prefetch_info.offset >= prefetch_info.page_size && !end)
            {
              svn_pool_clear(iterpool);

              prefetch_info.offset -= prefetch_info.page_size;
              SVN_ERR(prefetch_p2l_page(&end, &leaking_bucket, fs, rev_file,
                                        &prefetch_info, min_offset,
                                        iterpool));
            }
        }

      /* fetch page from disk and put it into the cache */
      SVN_ERR(get_p2l_page(&page_entries, rev_file, fs,
                           page_info.first_revision,
                           page_info.start_offset,
                           page_info.next_offset,
                           page_info.page_start,
                           page_info.page_size, iterpool));

      /* The last cache entry must not end beyond the range covered by
       * this index.  The same applies for any subset of entries. */
      if (page_entries->nelts)
        {
          const svn_fs_fs__p2l_entry_t *entry
            = &APR_ARRAY_IDX(page_entries, page_entries->nelts - 1,
                             svn_fs_fs__p2l_entry_t);
          if (  entry->offset + entry->size
              > page_info.page_size * page_info.page_count)
            return svn_error_createf(SVN_ERR_FS_INDEX_OVERFLOW , NULL,
                                     _("Last P2L index entry extends beyond "
                                       "the last page in revision %ld."),
                                     revision);
        }

      SVN_ERR(svn_cache__set(ffd->p2l_page_cache, &key, page_entries,
                             iterpool));

      /* append relevant information to result */
      append_p2l_entries(entries, page_entries, block_start, block_end);

      /* pre-fetch following pages */
      if (ffd->use_block_read)
        {
          end = FALSE;
          leaking_bucket = 4;
          prefetch_info = page_info;
          prefetch_info.offset = original_page_start;
          while (   prefetch_info.next_offset < max_offset
                && prefetch_info.page_no + 1 < prefetch_info.page_count
                && !end)
            {
              svn_pool_clear(iterpool);

              prefetch_info.offset += prefetch_info.page_size;
              SVN_ERR(prefetch_p2l_page(&end, &leaking_bucket, fs, rev_file,
                                        &prefetch_info, min_offset,
                                        iterpool));
            }
        }

      svn_pool_destroy(iterpool);
    }

  /* We access a valid page (otherwise, we had seen an error in the
   * get_p2l_keys request).  Hence, at least one entry must be found. */
  SVN_ERR_ASSERT(entries->nelts > 0);

  /* Add an "unused" entry if it extends beyond the end of the data file.
   * Since the index page size might be smaller than the current data
   * read block size, the trailing "unused" entry in this index may not
   * fully cover the end of the last block. */
  if (page_info.page_no + 1 >= page_info.page_count)
    {
      svn_fs_fs__p2l_entry_t *entry
        = &APR_ARRAY_IDX(entries, entries->nelts-1, svn_fs_fs__p2l_entry_t);

      apr_off_t entry_end = entry->offset + entry->size;
      if (entry_end < block_end)
        {
          if (entry->type == SVN_FS_FS__ITEM_TYPE_UNUSED)
            {
              /* extend the terminal filler */
              entry->size = block_end - entry->offset;
            }
          else
            {
              /* No terminal filler. Add one. */
              entry = apr_array_push(entries);
              entry->offset = entry_end;
              entry->size = block_end - entry_end;
              entry->type = SVN_FS_FS__ITEM_TYPE_UNUSED;
              entry->fnv1_checksum = 0;
              entry->item.revision = SVN_INVALID_REVNUM;
              entry->item.number = SVN_FS_FS__ITEM_INDEX_UNUSED;
            }
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__p2l_index_lookup(apr_array_header_t **entries,
                            svn_fs_t *fs,
                            svn_fs_fs__revision_file_t *rev_file,
                            svn_revnum_t revision,
                            apr_off_t block_start,
                            apr_off_t block_size,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  apr_off_t block_end = block_start + block_size;

  /* the receiving container */
  int last_count = 0;
  apr_array_header_t *result = apr_array_make(result_pool, 16,
                                              sizeof(svn_fs_fs__p2l_entry_t));

  /* Fetch entries page-by-page.  Since the p2l index is supposed to cover
   * every single byte in the rev / pack file - even unused sections -
   * every iteration must result in some progress. */
  while (block_start < block_end)
    {
      svn_fs_fs__p2l_entry_t *entry;
      SVN_ERR(p2l_index_lookup(result, rev_file, fs, revision, block_start,
                               block_end, scratch_pool));
      SVN_ERR_ASSERT(result->nelts > 0);

      /* continue directly behind last item */
      entry = &APR_ARRAY_IDX(result, result->nelts-1, svn_fs_fs__p2l_entry_t);
      block_start = entry->offset + entry->size;

      /* Some paranoia check.  Successive iterations should never return
       * duplicates but if it did, we might get into trouble later on. */
      if (last_count > 0 && last_count < result->nelts)
        {
           entry =  &APR_ARRAY_IDX(result, last_count - 1,
                                   svn_fs_fs__p2l_entry_t);
           SVN_ERR_ASSERT(APR_ARRAY_IDX(result, last_count,
                                        svn_fs_fs__p2l_entry_t).offset
                          >= entry->offset + entry->size);
        }

      last_count = result->nelts;
    }

  *entries = result;
  return SVN_NO_ERROR;
}

/* compare_fn_t comparing a svn_fs_fs__p2l_entry_t at LHS with an offset
 * RHS.
 */
static int
compare_p2l_entry_offsets(const void *lhs, const void *rhs)
{
  const svn_fs_fs__p2l_entry_t *entry = (const svn_fs_fs__p2l_entry_t *)lhs;
  apr_off_t offset = *(const apr_off_t *)rhs;

  return entry->offset < offset ? -1 : (entry->offset == offset ? 0 : 1);
}

/* Cached data extraction utility.  DATA is a P2L index page, e.g. an APR
 * array of svn_fs_fs__p2l_entry_t elements.  Return the entry for the item,
 * allocated in RESULT_POOL, starting at OFFSET or NULL if that's not an
 * the start offset of any item. Use SCRATCH_POOL for temporary allocations.
 */
static svn_fs_fs__p2l_entry_t *
get_p2l_entry_from_cached_page(const void *data,
                               apr_uint64_t offset,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  /* resolve all pointer values of in-cache data */
  const apr_array_header_t *page = data;
  apr_array_header_t *entries = apr_pmemdup(scratch_pool, page,
                                            sizeof(*page));
  svn_fs_fs__p2l_entry_t *entry;

  entries->elts = (char *)svn_temp_deserializer__ptr(page,
                                     (const void *const *)&page->elts);

  /* search of the offset we want */
  entry = svn_sort__array_lookup(entries, &offset, NULL,
      (int (*)(const void *, const void *))compare_p2l_entry_offsets);

  /* return it, if it is a perfect match */
  return entry ? apr_pmemdup(result_pool, entry, sizeof(*entry)) : NULL;
}

/* Implements svn_cache__partial_getter_func_t for P2L index pages, copying
 * the entry for the apr_off_t at BATON into *OUT.  *OUT will be NULL if
 * there is no matching entry in the index page at DATA.
 */
static svn_error_t *
p2l_entry_lookup_func(void **out,
                      const void *data,
                      apr_size_t data_len,
                      void *baton,
                      apr_pool_t *result_pool)
{
  svn_fs_fs__p2l_entry_t *entry
    = get_p2l_entry_from_cached_page(data, *(apr_off_t *)baton, result_pool,
                                     result_pool);

  *out = entry && entry->offset == *(apr_off_t *)baton
       ? apr_pmemdup(result_pool, entry, sizeof(*entry))
       : NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__p2l_entry_lookup(svn_fs_fs__p2l_entry_t **entry_p,
                            svn_fs_t *fs,
                            svn_fs_fs__revision_file_t *rev_file,
                            svn_revnum_t revision,
                            apr_off_t offset,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_fs_fs__page_cache_key_t key = { 0 };
  svn_boolean_t is_cached = FALSE;
  p2l_page_info_baton_t page_info;

  *entry_p = NULL;

  /* look for this info in our cache */
  SVN_ERR(get_p2l_keys(&page_info, &key, rev_file, fs, revision, offset,
                       scratch_pool));
  SVN_ERR(svn_cache__get_partial((void**)entry_p, &is_cached,
                                 ffd->p2l_page_cache, &key,
                                 p2l_entry_lookup_func, &offset,
                                 result_pool));
  if (!is_cached)
    {
      /* do a standard index lookup.  This is will automatically prefetch
       * data to speed up future lookups. */
      apr_array_header_t *entries = apr_array_make(result_pool, 1,
                                                   sizeof(**entry_p));
      SVN_ERR(p2l_index_lookup(entries, rev_file, fs, revision, offset,
                               offset + 1, scratch_pool));

      /* Find the entry that we want. */
      *entry_p = svn_sort__array_lookup(entries, &offset, NULL,
          (int (*)(const void *, const void *))compare_p2l_entry_offsets);
    }

  return SVN_NO_ERROR;
}

/* Implements svn_cache__partial_getter_func_t for P2L headers, setting *OUT
 * to the largest the first offset not covered by this P2L index.
 */
static svn_error_t *
p2l_get_max_offset_func(void **out,
                        const void *data,
                        apr_size_t data_len,
                        void *baton,
                        apr_pool_t *result_pool)
{
  const p2l_header_t *header = data;
  apr_off_t max_offset = header->file_size;
  *out = apr_pmemdup(result_pool, &max_offset, sizeof(max_offset));

  return SVN_NO_ERROR;
}

/* Core functionality of to svn_fs_fs__p2l_get_max_offset with identical
 * signature. */
static svn_error_t *
p2l_get_max_offset(apr_off_t *offset,
                   svn_fs_t *fs,
                   svn_fs_fs__revision_file_t *rev_file,
                   svn_revnum_t revision,
                   apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  p2l_header_t *header;
  svn_boolean_t is_cached = FALSE;
  apr_off_t *offset_p;

  /* look for the header data in our cache */
  pair_cache_key_t key;
  key.revision = rev_file->start_revision;
  key.second = rev_file->is_packed;

  SVN_ERR(svn_cache__get_partial((void **)&offset_p, &is_cached,
                                 ffd->p2l_header_cache, &key,
                                 p2l_get_max_offset_func, NULL,
                                 scratch_pool));
  if (is_cached)
    {
      *offset = *offset_p;
      return SVN_NO_ERROR;
    }

  SVN_ERR(get_p2l_header(&header, rev_file, fs, revision, scratch_pool,
                         scratch_pool));
  *offset = header->file_size;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__p2l_get_max_offset(apr_off_t *offset,
                              svn_fs_t *fs,
                              svn_fs_fs__revision_file_t *rev_file,
                              svn_revnum_t revision,
                              apr_pool_t *scratch_pool)
{
  return svn_error_trace(p2l_get_max_offset(offset, fs, rev_file, revision,
                                            scratch_pool));
}

/* Calculate the FNV1 checksum over the offset range in REV_FILE, covered by
 * ENTRY.  Store the result in ENTRY->FNV1_CHECKSUM.  Use SCRATCH_POOL for
 * temporary allocations. */
static svn_error_t *
calc_fnv1(svn_fs_fs__p2l_entry_t *entry,
          svn_fs_fs__revision_file_t *rev_file,
          apr_pool_t *scratch_pool)
{
  unsigned char buffer[4096];
  svn_checksum_t *checksum;
  svn_checksum_ctx_t *context
    = svn_checksum_ctx_create(svn_checksum_fnv1a_32x4, scratch_pool);
  apr_off_t size = entry->size;

  /* Special rules apply to unused sections / items.  The data must be a
   * sequence of NUL bytes (not checked here) and the checksum is fixed to 0.
   */
  if (entry->type == SVN_FS_FS__ITEM_TYPE_UNUSED)
    {
      entry->fnv1_checksum = 0;
      return SVN_NO_ERROR;
    }

  /* Read the block and feed it to the checksum calculator. */
  SVN_ERR(svn_io_file_seek(rev_file->file, APR_SET, &entry->offset,
                           scratch_pool));
  while (size > 0)
    {
      apr_size_t to_read = size > sizeof(buffer)
                         ? sizeof(buffer)
                         : (apr_size_t)size;
      SVN_ERR(svn_io_file_read_full2(rev_file->file, buffer, to_read, NULL,
                                     NULL, scratch_pool));
      SVN_ERR(svn_checksum_update(context, buffer, to_read));
      size -= to_read;
    }

  /* Store final checksum in ENTRY. */
  SVN_ERR(svn_checksum_final(&checksum, context, scratch_pool));
  entry->fnv1_checksum = ntohl(*(const apr_uint32_t *)checksum->digest);

  return SVN_NO_ERROR;
}

/*
 * Index (re-)creation utilities.
 */

svn_error_t *
svn_fs_fs__p2l_index_from_p2l_entries(const char **protoname,
                                      svn_fs_t *fs,
                                      svn_fs_fs__revision_file_t *rev_file,
                                      apr_array_header_t *entries,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool)
{
  apr_file_t *proto_index;

  /* Use a subpool for immediate temp file cleanup at the end of this
   * function. */
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;

  /* Create a proto-index file. */
  SVN_ERR(svn_io_open_unique_file3(NULL, protoname, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   result_pool, scratch_pool));
  SVN_ERR(svn_fs_fs__p2l_proto_index_open(&proto_index, *protoname,
                                          scratch_pool));

  /* Write ENTRIES to proto-index file and calculate checksums as we go. */
  for (i = 0; i < entries->nelts; ++i)
    {
      svn_fs_fs__p2l_entry_t *entry
        = APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t *);
      svn_pool_clear(iterpool);

      SVN_ERR(calc_fnv1(entry, rev_file, iterpool));
      SVN_ERR(svn_fs_fs__p2l_proto_index_add_entry(proto_index, entry,
                                                   iterpool));
    }

  /* Convert proto-index into final index and move it into position.
   * Note that REV_FILE contains the start revision of the shard file if it
   * has been packed while REVISION may be somewhere in the middle.  For
   * non-packed shards, they will have identical values. */
  SVN_ERR(svn_io_file_close(proto_index, iterpool));

  /* Temp file cleanup. */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* A svn_sort__array compatible comparator function, sorting the
 * svn_fs_fs__p2l_entry_t** given in LHS, RHS by revision. */
static int
compare_p2l_entry_revision(const void *lhs,
                           const void *rhs)
{
  const svn_fs_fs__p2l_entry_t *lhs_entry
    =*(const svn_fs_fs__p2l_entry_t **)lhs;
  const svn_fs_fs__p2l_entry_t *rhs_entry
    =*(const svn_fs_fs__p2l_entry_t **)rhs;

  if (lhs_entry->item.revision < rhs_entry->item.revision)
    return -1;

  return lhs_entry->item.revision == rhs_entry->item.revision ? 0 : 1;
}

svn_error_t *
svn_fs_fs__l2p_index_from_p2l_entries(const char **protoname,
                                      svn_fs_t *fs,
                                      apr_array_header_t *entries,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool)
{
  apr_file_t *proto_index;

  /* Use a subpool for immediate temp file cleanup at the end of this
   * function. */
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;
  svn_revnum_t last_revision = SVN_INVALID_REVNUM;

  /* L2P index must be written in revision order.
   * Sort ENTRIES accordingly. */
  svn_sort__array(entries, compare_p2l_entry_revision);

  /* Create the temporary proto-rev file. */
  SVN_ERR(svn_io_open_unique_file3(NULL, protoname, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   result_pool, scratch_pool));
  SVN_ERR(svn_fs_fs__l2p_proto_index_open(&proto_index, *protoname,
                                          scratch_pool));

  /*  Write all entries. */
  for (i = 0; i < entries->nelts; ++i)
    {
      const svn_fs_fs__p2l_entry_t *entry
        = APR_ARRAY_IDX(entries, i, const svn_fs_fs__p2l_entry_t *);
      svn_pool_clear(iterpool);

      if (entry->type == SVN_FS_FS__ITEM_TYPE_UNUSED)
        continue;

      if (last_revision != entry->item.revision)
        {
          SVN_ERR(svn_fs_fs__l2p_proto_index_add_revision(proto_index,
                                                          scratch_pool));
          last_revision = entry->item.revision;
        }

      SVN_ERR(svn_fs_fs__l2p_proto_index_add_entry(proto_index,
                                                   entry->offset,
                                                   entry->item.number,
                                                   iterpool));
    }

  /* Convert proto-index into final index and move it into position. */
  SVN_ERR(svn_io_file_close(proto_index, iterpool));

  /* Temp file cleanup. */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/*
 * Standard (de-)serialization functions
 */

svn_error_t *
svn_fs_fs__serialize_l2p_header(void **data,
                                apr_size_t *data_len,
                                void *in,
                                apr_pool_t *pool)
{
  l2p_header_t *header = in;
  svn_temp_serializer__context_t *context;
  svn_stringbuf_t *serialized;
  apr_size_t page_count = header->page_table_index[header->revision_count];
  apr_size_t page_table_size = page_count * sizeof(*header->page_table);
  apr_size_t index_size
    = (header->revision_count + 1) * sizeof(*header->page_table_index);
  apr_size_t data_size = sizeof(*header) + index_size + page_table_size;

  /* serialize header and all its elements */
  context = svn_temp_serializer__init(header,
                                      sizeof(*header),
                                      data_size + 32,
                                      pool);

  /* page table index array */
  svn_temp_serializer__add_leaf(context,
                                (const void * const *)&header->page_table_index,
                                index_size);

  /* page table array */
  svn_temp_serializer__add_leaf(context,
                                (const void * const *)&header->page_table,
                                page_table_size);

  /* return the serialized result */
  serialized = svn_temp_serializer__get(context);

  *data = serialized->data;
  *data_len = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__deserialize_l2p_header(void **out,
                                  void *data,
                                  apr_size_t data_len,
                                  apr_pool_t *pool)
{
  l2p_header_t *header = (l2p_header_t *)data;

  /* resolve the pointers in the struct */
  svn_temp_deserializer__resolve(header, (void**)&header->page_table_index);
  svn_temp_deserializer__resolve(header, (void**)&header->page_table);

  /* done */
  *out = header;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__serialize_l2p_page(void **data,
                              apr_size_t *data_len,
                              void *in,
                              apr_pool_t *pool)
{
  l2p_page_t *page = in;
  svn_temp_serializer__context_t *context;
  svn_stringbuf_t *serialized;
  apr_size_t of_table_size = page->entry_count * sizeof(*page->offsets);

  /* serialize struct and all its elements */
  context = svn_temp_serializer__init(page,
                                      sizeof(*page),
                                      of_table_size + sizeof(*page) + 32,
                                      pool);

  /* offsets and sub_items arrays */
  svn_temp_serializer__add_leaf(context,
                                (const void * const *)&page->offsets,
                                of_table_size);

  /* return the serialized result */
  serialized = svn_temp_serializer__get(context);

  *data = serialized->data;
  *data_len = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__deserialize_l2p_page(void **out,
                                void *data,
                                apr_size_t data_len,
                                apr_pool_t *pool)
{
  l2p_page_t *page = data;

  /* resolve the pointers in the struct */
  svn_temp_deserializer__resolve(page, (void**)&page->offsets);

  /* done */
  *out = page;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__serialize_p2l_header(void **data,
                                apr_size_t *data_len,
                                void *in,
                                apr_pool_t *pool)
{
  p2l_header_t *header = in;
  svn_temp_serializer__context_t *context;
  svn_stringbuf_t *serialized;
  apr_size_t table_size = (header->page_count + 1) * sizeof(*header->offsets);

  /* serialize header and all its elements */
  context = svn_temp_serializer__init(header,
                                      sizeof(*header),
                                      table_size + sizeof(*header) + 32,
                                      pool);

  /* offsets array */
  svn_temp_serializer__add_leaf(context,
                                (const void * const *)&header->offsets,
                                table_size);

  /* return the serialized result */
  serialized = svn_temp_serializer__get(context);

  *data = serialized->data;
  *data_len = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__deserialize_p2l_header(void **out,
                                  void *data,
                                  apr_size_t data_len,
                                  apr_pool_t *pool)
{
  p2l_header_t *header = data;

  /* resolve the only pointer in the struct */
  svn_temp_deserializer__resolve(header, (void**)&header->offsets);

  /* done */
  *out = header;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__serialize_p2l_page(void **data,
                              apr_size_t *data_len,
                              void *in,
                              apr_pool_t *pool)
{
  apr_array_header_t *page = in;
  svn_temp_serializer__context_t *context;
  svn_stringbuf_t *serialized;
  apr_size_t table_size = page->elt_size * page->nelts;

  /* serialize array header and all its elements */
  context = svn_temp_serializer__init(page,
                                      sizeof(*page),
                                      table_size + sizeof(*page) + 32,
                                      pool);

  /* items in the array */
  svn_temp_serializer__add_leaf(context,
                                (const void * const *)&page->elts,
                                table_size);

  /* return the serialized result */
  serialized = svn_temp_serializer__get(context);

  *data = serialized->data;
  *data_len = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__deserialize_p2l_page(void **out,
                                void *data,
                                apr_size_t data_len,
                                apr_pool_t *pool)
{
  apr_array_header_t *page = (apr_array_header_t *)data;

  /* resolve the only pointer in the struct */
  svn_temp_deserializer__resolve(page, (void**)&page->elts);

  /* patch up members */
  page->pool = pool;
  page->nalloc = page->nelts;

  /* done */
  *out = page;

  return SVN_NO_ERROR;
}
