/*
 * parse-diff.c: functions for parsing diff files
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

#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "svn_hash.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_string.h"
#include "svn_utf.h"
#include "svn_dirent_uri.h"
#include "svn_diff.h"
#include "svn_ctype.h"
#include "svn_mergeinfo.h"

#include "private/svn_eol_private.h"
#include "private/svn_dep_compat.h"
#include "private/svn_diff_private.h"
#include "private/svn_sorts_private.h"

#include "diff.h"

#include "svn_private_config.h"

/* Helper macro for readability */
#define starts_with(str, start)  \
  (strncmp((str), (start), strlen(start)) == 0)

/* Like strlen() but for string literals. */
#define STRLEN_LITERAL(str) (sizeof(str) - 1)

/* This struct describes a range within a file, as well as the
 * current cursor position within the range. All numbers are in bytes. */
struct svn_diff__hunk_range {
  apr_off_t start;
  apr_off_t end;
  apr_off_t current;
};

struct svn_diff_hunk_t {
  /* The patch this hunk belongs to. */
  const svn_patch_t *patch;

  /* APR file handle to the patch file this hunk came from. */
  apr_file_t *apr_file;

  /* Ranges used to keep track of this hunk's texts positions within
   * the patch file. */
  struct svn_diff__hunk_range diff_text_range;
  struct svn_diff__hunk_range original_text_range;
  struct svn_diff__hunk_range modified_text_range;

  /* Hunk ranges as they appeared in the patch file.
   * All numbers are lines, not bytes. */
  svn_linenum_t original_start;
  svn_linenum_t original_length;
  svn_linenum_t modified_start;
  svn_linenum_t modified_length;

  /* Number of lines of leading and trailing hunk context. */
  svn_linenum_t leading_context;
  svn_linenum_t trailing_context;

  /* Did we see a 'file does not end with eol' marker in this hunk? */
  svn_boolean_t original_no_final_eol;
  svn_boolean_t modified_no_final_eol;

  /* Fuzz penalty, triggered by bad patch targets */
  svn_linenum_t original_fuzz;
  svn_linenum_t modified_fuzz;
};

struct svn_diff_binary_patch_t {
  /* The patch this hunk belongs to. */
  const svn_patch_t *patch;

  /* APR file handle to the patch file this hunk came from. */
  apr_file_t *apr_file;

  /* Offsets inside APR_FILE representing the location of the patch */
  apr_off_t src_start;
  apr_off_t src_end;
  svn_filesize_t src_filesize; /* Expanded/final size */

  /* Offsets inside APR_FILE representing the location of the patch */
  apr_off_t dst_start;
  apr_off_t dst_end;
  svn_filesize_t dst_filesize; /* Expanded/final size */
};

/* Common guts of svn_diff_hunk__create_adds_single_line() and
 * svn_diff_hunk__create_deletes_single_line().
 *
 * ADD is TRUE if adding and FALSE if deleting.
 */
static svn_error_t *
add_or_delete_single_line(svn_diff_hunk_t **hunk_out,
                          const char *line,
                          const svn_patch_t *patch,
                          svn_boolean_t add,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_diff_hunk_t *hunk = apr_pcalloc(result_pool, sizeof(*hunk));
  static const char *hunk_header[] = { "@@ -1 +0,0 @@\n", "@@ -0,0 +1 @@\n" };
  const apr_size_t header_len = strlen(hunk_header[add]);
  const apr_size_t len = strlen(line);
  const apr_size_t end = header_len + (1 + len); /* The +1 is for the \n. */
  svn_stringbuf_t *buf = svn_stringbuf_create_ensure(end + 1, scratch_pool);

  hunk->patch = patch;

  /* hunk->apr_file is created below. */

  hunk->diff_text_range.start = header_len;
  hunk->diff_text_range.current = header_len;

  if (add)
    {
      hunk->original_text_range.start = 0; /* There's no "original" text. */
      hunk->original_text_range.current = 0;
      hunk->original_text_range.end = 0;
      hunk->original_no_final_eol = FALSE;

      hunk->modified_text_range.start = header_len;
      hunk->modified_text_range.current = header_len;
      hunk->modified_text_range.end = end;
      hunk->modified_no_final_eol = TRUE;

      hunk->original_start = 0;
      hunk->original_length = 0;

      hunk->modified_start = 1;
      hunk->modified_length = 1;
    }
  else /* delete */
    {
      hunk->original_text_range.start = header_len;
      hunk->original_text_range.current = header_len;
      hunk->original_text_range.end = end;
      hunk->original_no_final_eol = TRUE;

      hunk->modified_text_range.start = 0; /* There's no "original" text. */
      hunk->modified_text_range.current = 0;
      hunk->modified_text_range.end = 0;
      hunk->modified_no_final_eol = FALSE;

      hunk->original_start = 1;
      hunk->original_length = 1;

      hunk->modified_start = 0;
      hunk->modified_length = 0; /* setting to '1' works too */
    }

  hunk->leading_context = 0;
  hunk->trailing_context = 0;

  /* Create APR_FILE and put just a hunk in it (without a diff header).
   * Save the offset of the last byte of the diff line. */
  svn_stringbuf_appendbytes(buf, hunk_header[add], header_len);
  svn_stringbuf_appendbyte(buf, add ? '+' : '-');
  svn_stringbuf_appendbytes(buf, line, len);
  svn_stringbuf_appendbyte(buf, '\n');
  svn_stringbuf_appendcstr(buf, "\\ No newline at end of hunk\n");

  hunk->diff_text_range.end = buf->len;

  SVN_ERR(svn_io_open_unique_file3(&hunk->apr_file, NULL /* filename */,
                                   NULL /* system tempdir */,
                                   svn_io_file_del_on_pool_cleanup,
                                   result_pool, scratch_pool));
  SVN_ERR(svn_io_file_write_full(hunk->apr_file,
                                 buf->data, buf->len,
                                 NULL, scratch_pool));
  /* No need to seek. */

  *hunk_out = hunk;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff_hunk__create_adds_single_line(svn_diff_hunk_t **hunk_out,
                                       const char *line,
                                       const svn_patch_t *patch,
                                       apr_pool_t *result_pool,
                                       apr_pool_t *scratch_pool)
{
  SVN_ERR(add_or_delete_single_line(hunk_out, line, patch, 
                                    (!patch->reverse),
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff_hunk__create_deletes_single_line(svn_diff_hunk_t **hunk_out,
                                          const char *line,
                                          const svn_patch_t *patch,
                                          apr_pool_t *result_pool,
                                          apr_pool_t *scratch_pool)
{
  SVN_ERR(add_or_delete_single_line(hunk_out, line, patch,
                                    patch->reverse,
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

void
svn_diff_hunk_reset_diff_text(svn_diff_hunk_t *hunk)
{
  hunk->diff_text_range.current = hunk->diff_text_range.start;
}

void
svn_diff_hunk_reset_original_text(svn_diff_hunk_t *hunk)
{
  if (hunk->patch->reverse)
    hunk->modified_text_range.current = hunk->modified_text_range.start;
  else
    hunk->original_text_range.current = hunk->original_text_range.start;
}

void
svn_diff_hunk_reset_modified_text(svn_diff_hunk_t *hunk)
{
  if (hunk->patch->reverse)
    hunk->original_text_range.current = hunk->original_text_range.start;
  else
    hunk->modified_text_range.current = hunk->modified_text_range.start;
}

svn_linenum_t
svn_diff_hunk_get_original_start(const svn_diff_hunk_t *hunk)
{
  return hunk->patch->reverse ? hunk->modified_start : hunk->original_start;
}

svn_linenum_t
svn_diff_hunk_get_original_length(const svn_diff_hunk_t *hunk)
{
  return hunk->patch->reverse ? hunk->modified_length : hunk->original_length;
}

svn_linenum_t
svn_diff_hunk_get_modified_start(const svn_diff_hunk_t *hunk)
{
  return hunk->patch->reverse ? hunk->original_start : hunk->modified_start;
}

svn_linenum_t
svn_diff_hunk_get_modified_length(const svn_diff_hunk_t *hunk)
{
  return hunk->patch->reverse ? hunk->original_length : hunk->modified_length;
}

svn_linenum_t
svn_diff_hunk_get_leading_context(const svn_diff_hunk_t *hunk)
{
  return hunk->leading_context;
}

svn_linenum_t
svn_diff_hunk_get_trailing_context(const svn_diff_hunk_t *hunk)
{
  return hunk->trailing_context;
}

svn_linenum_t
svn_diff_hunk__get_fuzz_penalty(const svn_diff_hunk_t *hunk)
{
  return hunk->patch->reverse ? hunk->original_fuzz : hunk->modified_fuzz;
}

/* Baton for the base85 stream implementation */
struct base85_baton_t
{
  apr_file_t *file;
  apr_pool_t *iterpool;
  char buffer[52];        /* Bytes on current line */
  apr_off_t next_pos;     /* Start position of next line */
  apr_off_t end_pos;      /* Position after last line */
  apr_size_t buf_size;    /* Bytes available (52 unless at eof) */
  apr_size_t buf_pos;     /* Bytes in linebuffer */
  svn_boolean_t done;     /* At eof? */
};

/* Implements svn_read_fn_t for the base85 read stream */
static svn_error_t *
read_handler_base85(void *baton, char *buffer, apr_size_t *len)
{
  struct base85_baton_t *b85b = baton;
  apr_pool_t *iterpool = b85b->iterpool;
  apr_size_t remaining = *len;
  char *dest = buffer;

  svn_pool_clear(iterpool);

  if (b85b->done)
    {
      *len = 0;
      return SVN_NO_ERROR;
    }

  while (remaining && (b85b->buf_size > b85b->buf_pos
                       || b85b->next_pos < b85b->end_pos))
    {
      svn_stringbuf_t *line;
      svn_boolean_t at_eof;

      apr_size_t available = b85b->buf_size - b85b->buf_pos;
      if (available)
        {
          apr_size_t n = (remaining < available) ? remaining : available;

          memcpy(dest, b85b->buffer + b85b->buf_pos, n);
          dest += n;
          remaining -= n;
          b85b->buf_pos += n;

          if (!remaining)
            return SVN_NO_ERROR; /* *len = OK */
        }

      if (b85b->next_pos >= b85b->end_pos)
        break; /* At EOF */
      SVN_ERR(svn_io_file_seek(b85b->file, APR_SET, &b85b->next_pos,
                               iterpool));
      SVN_ERR(svn_io_file_readline(b85b->file, &line, NULL, &at_eof,
                                   APR_SIZE_MAX, iterpool, iterpool));
      if (at_eof)
        b85b->next_pos = b85b->end_pos;
      else
        {
          SVN_ERR(svn_io_file_get_offset(&b85b->next_pos, b85b->file,
                                         iterpool));
        }

      if (line->len && line->data[0] >= 'A' && line->data[0] <= 'Z')
        b85b->buf_size = line->data[0] - 'A' + 1;
      else if (line->len && line->data[0] >= 'a' && line->data[0] <= 'z')
        b85b->buf_size = line->data[0] - 'a' + 26 + 1;
      else
        return svn_error_create(SVN_ERR_DIFF_UNEXPECTED_DATA, NULL,
                                _("Unexpected data in base85 section"));

      if (b85b->buf_size < 52)
        b85b->next_pos = b85b->end_pos; /* Handle as EOF */

      SVN_ERR(svn_diff__base85_decode_line(b85b->buffer, b85b->buf_size,
                                           line->data + 1, line->len - 1,
                                           iterpool));
      b85b->buf_pos = 0;
    }

  *len -= remaining;
  b85b->done = TRUE;

  return SVN_NO_ERROR;
}

/* Implements svn_close_fn_t for the base85 read stream */
static svn_error_t *
close_handler_base85(void *baton)
{
  struct base85_baton_t *b85b = baton;

  svn_pool_destroy(b85b->iterpool);

  return SVN_NO_ERROR;
}

/* Gets a stream that reads decoded base85 data from a segment of a file.
   The current implementation might assume that both start_pos and end_pos
   are located at line boundaries. */
static svn_stream_t *
get_base85_data_stream(apr_file_t *file,
                       apr_off_t start_pos,
                       apr_off_t end_pos,
                       apr_pool_t *result_pool)
{
  struct base85_baton_t *b85b = apr_pcalloc(result_pool, sizeof(*b85b));
  svn_stream_t *base85s = svn_stream_create(b85b, result_pool);

  b85b->file = file;
  b85b->iterpool = svn_pool_create(result_pool);
  b85b->next_pos = start_pos;
  b85b->end_pos = end_pos;

  svn_stream_set_read2(base85s, NULL /* only full read support */,
                       read_handler_base85);
  svn_stream_set_close(base85s, close_handler_base85);
  return base85s;
}

/* Baton for the length verification stream functions */
struct length_verify_baton_t
{
  svn_stream_t *inner;
  svn_filesize_t remaining;
};

/* Implements svn_read_fn_t for the length verification stream */
static svn_error_t *
read_handler_length_verify(void *baton, char *buffer, apr_size_t *len)
{
  struct length_verify_baton_t *lvb = baton;
  apr_size_t requested_len = *len;

  SVN_ERR(svn_stream_read_full(lvb->inner, buffer, len));

  if (*len > lvb->remaining)
    return svn_error_create(SVN_ERR_DIFF_UNEXPECTED_DATA, NULL,
                            _("Base85 data expands to longer than declared "
                              "filesize"));
  else if (requested_len > *len && *len != lvb->remaining)
    return svn_error_create(SVN_ERR_DIFF_UNEXPECTED_DATA, NULL,
                            _("Base85 data expands to smaller than declared "
                              "filesize"));

  lvb->remaining -= *len;

  return SVN_NO_ERROR;
}

/* Implements svn_close_fn_t for the length verification stream */
static svn_error_t *
close_handler_length_verify(void *baton)
{
  struct length_verify_baton_t *lvb = baton;

  return svn_error_trace(svn_stream_close(lvb->inner));
}

/* Gets a stream that verifies on reads that the inner stream is exactly
   of the specified length */
static svn_stream_t *
get_verify_length_stream(svn_stream_t *inner,
                         svn_filesize_t expected_size,
                         apr_pool_t *result_pool)
{
  struct length_verify_baton_t *lvb = apr_palloc(result_pool, sizeof(*lvb));
  svn_stream_t *len_stream = svn_stream_create(lvb, result_pool);

  lvb->inner = inner;
  lvb->remaining = expected_size;

  svn_stream_set_read2(len_stream, NULL /* only full read support */,
                       read_handler_length_verify);
  svn_stream_set_close(len_stream, close_handler_length_verify);

  return len_stream;
}

svn_stream_t *
svn_diff_get_binary_diff_original_stream(const svn_diff_binary_patch_t *bpatch,
                                         apr_pool_t *result_pool)
{
  svn_stream_t *s = get_base85_data_stream(bpatch->apr_file, bpatch->src_start,
                                           bpatch->src_end, result_pool);

  s = svn_stream_compressed(s, result_pool);

  /* ### If we (ever) want to support the DELTA format, then we should hook the
         undelta handling here */

  return get_verify_length_stream(s, bpatch->src_filesize, result_pool);
}

svn_stream_t *
svn_diff_get_binary_diff_result_stream(const svn_diff_binary_patch_t *bpatch,
                                       apr_pool_t *result_pool)
{
  svn_stream_t *s = get_base85_data_stream(bpatch->apr_file, bpatch->dst_start,
                                           bpatch->dst_end, result_pool);

  s = svn_stream_compressed(s, result_pool);

  /* ### If we (ever) want to support the DELTA format, then we should hook the
  undelta handling here */

  return get_verify_length_stream(s, bpatch->dst_filesize, result_pool);
}

/* Try to parse a positive number from a decimal number encoded
 * in the string NUMBER. Return parsed number in OFFSET, and return
 * TRUE if parsing was successful. */
static svn_boolean_t
parse_offset(svn_linenum_t *offset, const char *number)
{
  svn_error_t *err;
  apr_uint64_t val;

  err = svn_cstring_strtoui64(&val, number, 0, SVN_LINENUM_MAX_VALUE, 10);
  if (err)
    {
      svn_error_clear(err);
      return FALSE;
    }

  *offset = (svn_linenum_t)val;

  return TRUE;
}

/* Try to parse a hunk range specification from the string RANGE.
 * Return parsed information in *START and *LENGTH, and return TRUE
 * if the range parsed correctly. Note: This function may modify the
 * input value RANGE. */
static svn_boolean_t
parse_range(svn_linenum_t *start, svn_linenum_t *length, char *range)
{
  char *comma;

  if (*range == 0)
    return FALSE;

  comma = strstr(range, ",");
  if (comma)
    {
      if (strlen(comma + 1) > 0)
        {
          /* Try to parse the length. */
          if (! parse_offset(length, comma + 1))
            return FALSE;

          /* Snip off the end of the string,
           * so we can comfortably parse the line
           * number the hunk starts at. */
          *comma = '\0';
        }
       else
         /* A comma but no length? */
         return FALSE;
    }
  else
    {
      *length = 1;
    }

  /* Try to parse the line number the hunk starts at. */
  return parse_offset(start, range);
}

/* Try to parse a hunk header in string HEADER, putting parsed information
 * into HUNK. Return TRUE if the header parsed correctly. ATAT is the
 * character string used to delimit the hunk header.
 * Do all allocations in POOL. */
static svn_boolean_t
parse_hunk_header(const char *header, svn_diff_hunk_t *hunk,
                  const char *atat, apr_pool_t *pool)
{
  const char *p;
  const char *start;
  svn_stringbuf_t *range;

  p = header + strlen(atat);
  if (*p != ' ')
    /* No. */
    return FALSE;
  p++;
  if (*p != '-')
    /* Nah... */
    return FALSE;
  /* OK, this may be worth allocating some memory for... */
  range = svn_stringbuf_create_ensure(31, pool);
  start = ++p;
  while (*p && *p != ' ')
    {
      p++;
    }

  if (*p != ' ')
    /* No no no... */
    return FALSE;

  svn_stringbuf_appendbytes(range, start, p - start);

  /* Try to parse the first range. */
  if (! parse_range(&hunk->original_start, &hunk->original_length, range->data))
    return FALSE;

  /* Clear the stringbuf so we can reuse it for the second range. */
  svn_stringbuf_setempty(range);
  p++;
  if (*p != '+')
    /* Eeek! */
    return FALSE;
  /* OK, this may be worth copying... */
  start = ++p;
  while (*p && *p != ' ')
    {
      p++;
    }
  if (*p != ' ')
    /* No no no... */
    return FALSE;

  svn_stringbuf_appendbytes(range, start, p - start);

  /* Check for trailing @@ */
  p++;
  if (! starts_with(p, atat))
    return FALSE;

  /* There may be stuff like C-function names after the trailing @@,
   * but we ignore that. */

  /* Try to parse the second range. */
  if (! parse_range(&hunk->modified_start, &hunk->modified_length, range->data))
    return FALSE;

  /* Hunk header is good. */
  return TRUE;
}

/* Read a line of original or modified hunk text from the specified
 * RANGE within FILE. FILE is expected to contain unidiff text.
 * Leading unidiff symbols ('+', '-', and ' ') are removed from the line,
 * Any lines commencing with the VERBOTEN character are discarded.
 * VERBOTEN should be '+' or '-', depending on which form of hunk text
 * is being read. NO_FINAL_EOL declares if the hunk contains a no final
 * EOL marker.
 *
 * All other parameters are as in svn_diff_hunk_readline_original_text()
 * and svn_diff_hunk_readline_modified_text().
 */
static svn_error_t *
hunk_readline_original_or_modified(apr_file_t *file,
                                   struct svn_diff__hunk_range *range,
                                   svn_stringbuf_t **stringbuf,
                                   const char **eol,
                                   svn_boolean_t *eof,
                                   char verboten,
                                   svn_boolean_t no_final_eol,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  apr_size_t max_len;
  svn_boolean_t filtered;
  apr_off_t pos;
  svn_stringbuf_t *str;
  const char *eol_p;
  apr_pool_t *last_pool;

  if (!eol)
    eol = &eol_p;

  if (range->current >= range->end)
    {
      /* We're past the range. Indicate that no bytes can be read. */
      *eof = TRUE;
      *eol = NULL;
      *stringbuf = svn_stringbuf_create_empty(result_pool);
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_io_file_get_offset(&pos, file, scratch_pool));
  SVN_ERR(svn_io_file_seek(file, APR_SET, &range->current, scratch_pool));

  /* It's not ITERPOOL because we use data allocated in LAST_POOL out
     of the loop. */
  last_pool = svn_pool_create(scratch_pool);
  do
    {
      svn_pool_clear(last_pool);

      max_len = range->end - range->current;
      SVN_ERR(svn_io_file_readline(file, &str, eol, eof, max_len,
                                   last_pool, last_pool));
      SVN_ERR(svn_io_file_get_offset(&range->current, file, last_pool));
      filtered = (str->data[0] == verboten || str->data[0] == '\\');
    }
  while (filtered && ! *eof);

  if (filtered)
    {
      /* EOF, return an empty string. */
      *stringbuf = svn_stringbuf_create_ensure(0, result_pool);
      *eol = NULL;
    }
  else if (str->data[0] == '+' || str->data[0] == '-' || str->data[0] == ' ')
    {
      /* Shave off leading unidiff symbols. */
      *stringbuf = svn_stringbuf_create(str->data + 1, result_pool);
    }
  else
    {
      /* Return the line as-is. Handle as a chopped leading spaces */
      *stringbuf = svn_stringbuf_dup(str, result_pool);
    }

  if (!filtered && *eof && !*eol && *str->data)
    {
      /* Ok, we miss a final EOL in the patch file, but didn't see a
         no eol marker line.

         We should report that we had an EOL or the patch code will
         misbehave (and it knows nothing about no eol markers) */

      if (!no_final_eol && eol != &eol_p)
        {
          apr_off_t start = 0;

          SVN_ERR(svn_io_file_seek(file, APR_SET, &start, scratch_pool));

          SVN_ERR(svn_io_file_readline(file, &str, eol, NULL, APR_SIZE_MAX,
                                       scratch_pool, scratch_pool));

          /* Every patch file that has hunks has at least one EOL*/
          SVN_ERR_ASSERT(*eol != NULL);
        }

      *eof = FALSE;
      /* Fall through to seek back to the right location */
    }
  SVN_ERR(svn_io_file_seek(file, APR_SET, &pos, scratch_pool));

  svn_pool_destroy(last_pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff_hunk_readline_original_text(svn_diff_hunk_t *hunk,
                                     svn_stringbuf_t **stringbuf,
                                     const char **eol,
                                     svn_boolean_t *eof,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool)
{
  return svn_error_trace(
    hunk_readline_original_or_modified(hunk->apr_file,
                                       hunk->patch->reverse ?
                                         &hunk->modified_text_range :
                                         &hunk->original_text_range,
                                       stringbuf, eol, eof,
                                       hunk->patch->reverse ? '-' : '+',
                                       hunk->patch->reverse
                                          ? hunk->modified_no_final_eol
                                          : hunk->original_no_final_eol,
                                       result_pool, scratch_pool));
}

svn_error_t *
svn_diff_hunk_readline_modified_text(svn_diff_hunk_t *hunk,
                                     svn_stringbuf_t **stringbuf,
                                     const char **eol,
                                     svn_boolean_t *eof,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool)
{
  return svn_error_trace(
    hunk_readline_original_or_modified(hunk->apr_file,
                                       hunk->patch->reverse ?
                                         &hunk->original_text_range :
                                         &hunk->modified_text_range,
                                       stringbuf, eol, eof,
                                       hunk->patch->reverse ? '+' : '-',
                                       hunk->patch->reverse
                                          ? hunk->original_no_final_eol
                                          : hunk->modified_no_final_eol,
                                       result_pool, scratch_pool));
}

svn_error_t *
svn_diff_hunk_readline_diff_text(svn_diff_hunk_t *hunk,
                                 svn_stringbuf_t **stringbuf,
                                 const char **eol,
                                 svn_boolean_t *eof,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *line;
  apr_size_t max_len;
  apr_off_t pos;
  const char *eol_p;

  if (!eol)
    eol = &eol_p;

  if (hunk->diff_text_range.current >= hunk->diff_text_range.end)
    {
      /* We're past the range. Indicate that no bytes can be read. */
      *eof = TRUE;
      *eol = NULL;
      *stringbuf = svn_stringbuf_create_empty(result_pool);
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_io_file_get_offset(&pos, hunk->apr_file, scratch_pool));
  SVN_ERR(svn_io_file_seek(hunk->apr_file, APR_SET,
                           &hunk->diff_text_range.current, scratch_pool));
  max_len = hunk->diff_text_range.end - hunk->diff_text_range.current;
  SVN_ERR(svn_io_file_readline(hunk->apr_file, &line, eol, eof, max_len,
                               result_pool,
                   scratch_pool));
  SVN_ERR(svn_io_file_get_offset(&hunk->diff_text_range.current,
                                 hunk->apr_file, scratch_pool));

  if (*eof && !*eol && *line->data)
    {
      /* Ok, we miss a final EOL in the patch file, but didn't see a
          no eol marker line.

          We should report that we had an EOL or the patch code will
          misbehave (and it knows nothing about no eol markers) */

      if (eol != &eol_p)
        {
          /* Lets pick the first eol we find in our patch file */
          apr_off_t start = 0;
          svn_stringbuf_t *str;

          SVN_ERR(svn_io_file_seek(hunk->apr_file, APR_SET, &start,
                                   scratch_pool));

          SVN_ERR(svn_io_file_readline(hunk->apr_file, &str, eol, NULL,
                                       APR_SIZE_MAX,
                                       scratch_pool, scratch_pool));

          /* Every patch file that has hunks has at least one EOL*/
          SVN_ERR_ASSERT(*eol != NULL);
        }

      *eof = FALSE;

      /* Fall through to seek back to the right location */
    }

  SVN_ERR(svn_io_file_seek(hunk->apr_file, APR_SET, &pos, scratch_pool));

  if (hunk->patch->reverse)
    {
      if (line->data[0] == '+')
        line->data[0] = '-';
      else if (line->data[0] == '-')
        line->data[0] = '+';
    }

  *stringbuf = line;

  return SVN_NO_ERROR;
}

/* Parse *PROP_NAME from HEADER as the part after the INDICATOR line.
 * Allocate *PROP_NAME in RESULT_POOL.
 * Set *PROP_NAME to NULL if no valid property name was found. */
static svn_error_t *
parse_prop_name(const char **prop_name, const char *header,
                const char *indicator, apr_pool_t *result_pool)
{
  SVN_ERR(svn_utf_cstring_to_utf8(prop_name,
                                  header + strlen(indicator),
                                  result_pool));
  if (**prop_name == '\0')
    *prop_name = NULL;
  else if (! svn_prop_name_is_valid(*prop_name))
    {
      svn_stringbuf_t *buf = svn_stringbuf_create(*prop_name, result_pool);
      svn_stringbuf_strip_whitespace(buf);
      *prop_name = (svn_prop_name_is_valid(buf->data) ? buf->data : NULL);
    }

  return SVN_NO_ERROR;
}


/* A helper function to parse svn:mergeinfo diffs.
 *
 * These diffs use a special pretty-print format, for instance:
 *
 * Added: svn:mergeinfo
 * ## -0,0 +0,1 ##
 *   Merged /trunk:r2-3
 *
 * The hunk header has the following format:
 * ## -0,NUMBER_OF_REVERSE_MERGES +0,NUMBER_OF_FORWARD_MERGES ##
 *
 * At this point, the number of reverse merges has already been
 * parsed into HUNK->ORIGINAL_LENGTH, and the number of forward
 * merges has been parsed into HUNK->MODIFIED_LENGTH.
 *
 * The header is followed by a list of mergeinfo, one path per line.
 * This function parses such lines. Lines describing reverse merges
 * appear first, and then all lines describing forward merges appear.
 *
 * Parts of the line are affected by i18n. The words 'Merged'
 * and 'Reverse-merged' can appear in any language and at any
 * position within the line. We can only assume that a leading
 * '/' starts the merge source path, the path is followed by
 * ":r", which in turn is followed by a mergeinfo revision range,
 *  which is terminated by whitespace or end-of-string.
 *
 * If the current line meets the above criteria and we're able
 * to parse valid mergeinfo from it, the resulting mergeinfo
 * is added to patch->mergeinfo or patch->reverse_mergeinfo,
 * and we proceed to the next line.
 */
static svn_error_t *
parse_mergeinfo(svn_boolean_t *found_mergeinfo,
                svn_stringbuf_t *line,
                svn_diff_hunk_t *hunk,
                svn_patch_t *patch,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  char *slash = strchr(line->data, '/');
  char *colon = strrchr(line->data, ':');

  *found_mergeinfo = FALSE;

  if (slash && colon && colon[1] == 'r' && slash < colon)
    {
      svn_stringbuf_t *input;
      svn_mergeinfo_t mergeinfo = NULL;
      char *s;
      svn_error_t *err;

      input = svn_stringbuf_create_ensure(line->len, scratch_pool);

      /* Copy the merge source path + colon */
      s = slash;
      while (s <= colon)
        {
          svn_stringbuf_appendbyte(input, *s);
          s++;
        }

      /* skip 'r' after colon */
      s++;

      /* Copy the revision range. */
      while (s < line->data + line->len)
        {
          if (svn_ctype_isspace(*s))
            break;
          svn_stringbuf_appendbyte(input, *s);
          s++;
        }

      err = svn_mergeinfo_parse(&mergeinfo, input->data, result_pool);
      if (err && err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
        {
          svn_error_clear(err);
          mergeinfo = NULL;
        }
      else
        SVN_ERR(err);

      if (mergeinfo)
        {
          if (hunk->original_length > 0) /* reverse merges */
            {
              if (patch->reverse)
                {
                  if (patch->mergeinfo == NULL)
                    patch->mergeinfo = mergeinfo;
                  else
                    SVN_ERR(svn_mergeinfo_merge2(patch->mergeinfo,
                                                 mergeinfo,
                                                 result_pool,
                                                 scratch_pool));
                }
              else
                {
                  if (patch->reverse_mergeinfo == NULL)
                    patch->reverse_mergeinfo = mergeinfo;
                  else
                    SVN_ERR(svn_mergeinfo_merge2(patch->reverse_mergeinfo,
                                                 mergeinfo,
                                                 result_pool,
                                                 scratch_pool));
                }
              hunk->original_length--;
            }
          else if (hunk->modified_length > 0) /* forward merges */
            {
              if (patch->reverse)
                {
                  if (patch->reverse_mergeinfo == NULL)
                    patch->reverse_mergeinfo = mergeinfo;
                  else
                    SVN_ERR(svn_mergeinfo_merge2(patch->reverse_mergeinfo,
                                                 mergeinfo,
                                                 result_pool,
                                                 scratch_pool));
                }
              else
                {
                  if (patch->mergeinfo == NULL)
                    patch->mergeinfo = mergeinfo;
                  else
                    SVN_ERR(svn_mergeinfo_merge2(patch->mergeinfo,
                                                 mergeinfo,
                                                 result_pool,
                                                 scratch_pool));
                }
              hunk->modified_length--;
            }

          *found_mergeinfo = TRUE;
        }
    }

  return SVN_NO_ERROR;
}

/* Return the next *HUNK from a PATCH in APR_FILE.
 * If no hunk can be found, set *HUNK to NULL.
 * Set IS_PROPERTY to TRUE if we have a property hunk. If the returned HUNK
 * is the first belonging to a certain property, then PROP_NAME and
 * PROP_OPERATION will be set too. If we have a text hunk, PROP_NAME will be
 * NULL.  If IGNORE_WHITESPACE is TRUE, lines without leading spaces will be
 * treated as context lines.  Allocate results in RESULT_POOL.
 * Use SCRATCH_POOL for all other allocations. */
static svn_error_t *
parse_next_hunk(svn_diff_hunk_t **hunk,
                svn_boolean_t *is_property,
                const char **prop_name,
                svn_diff_operation_kind_t *prop_operation,
                svn_patch_t *patch,
                apr_file_t *apr_file,
                svn_boolean_t ignore_whitespace,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  static const char * const minus = "--- ";
  static const char * const text_atat = "@@";
  static const char * const prop_atat = "##";
  svn_stringbuf_t *line;
  svn_boolean_t eof, in_hunk, hunk_seen;
  apr_off_t pos, last_line;
  apr_off_t start, end;
  apr_off_t original_end;
  apr_off_t modified_end;
  svn_boolean_t original_no_final_eol = FALSE;
  svn_boolean_t modified_no_final_eol = FALSE;
  svn_linenum_t original_lines;
  svn_linenum_t modified_lines;
  svn_linenum_t leading_context;
  svn_linenum_t trailing_context;
  svn_boolean_t changed_line_seen;
  enum {
    noise_line,
    original_line,
    modified_line,
    context_line
  } last_line_type;
  apr_pool_t *iterpool;

  *prop_operation = svn_diff_op_unchanged;

  /* We only set this if we have a property hunk header. */
  *prop_name = NULL;
  *is_property = FALSE;

  if (apr_file_eof(apr_file) == APR_EOF)
    {
      /* No more hunks here. */
      *hunk = NULL;
      return SVN_NO_ERROR;
    }

  in_hunk = FALSE;
  hunk_seen = FALSE;
  leading_context = 0;
  trailing_context = 0;
  changed_line_seen = FALSE;
  original_end = 0;
  modified_end = 0;
  *hunk = apr_pcalloc(result_pool, sizeof(**hunk));

  /* Get current seek position. */
  SVN_ERR(svn_io_file_get_offset(&pos, apr_file, scratch_pool));

  /* Start out assuming noise. */
  last_line_type = noise_line;

  iterpool = svn_pool_create(scratch_pool);
  do
    {

      svn_pool_clear(iterpool);

      /* Remember the current line's offset, and read the line. */
      last_line = pos;
      SVN_ERR(svn_io_file_readline(apr_file, &line, NULL, &eof, APR_SIZE_MAX,
                                   iterpool, iterpool));

      /* Update line offset for next iteration. */
      SVN_ERR(svn_io_file_get_offset(&pos, apr_file, iterpool));

      /* Lines starting with a backslash indicate a missing EOL:
       * "\ No newline at end of file" or "end of property". */
      if (line->data[0] == '\\')
        {
          if (in_hunk)
            {
              char eolbuf[2];
              apr_size_t len;
              apr_off_t off;
              apr_off_t hunk_text_end;

              /* Comment terminates the hunk text and says the hunk text
               * has no trailing EOL. Snip off trailing EOL which is part
               * of the patch file but not part of the hunk text. */
              off = last_line - 2;
              SVN_ERR(svn_io_file_seek(apr_file, APR_SET, &off, iterpool));
              len = sizeof(eolbuf);
              SVN_ERR(svn_io_file_read_full2(apr_file, eolbuf, len, &len,
                                             &eof, iterpool));
              if (eolbuf[0] == '\r' && eolbuf[1] == '\n')
                hunk_text_end = last_line - 2;
              else if (eolbuf[1] == '\n' || eolbuf[1] == '\r')
                hunk_text_end = last_line - 1;
              else
                hunk_text_end = last_line;

              if (last_line_type == original_line && original_end == 0)
                original_end = hunk_text_end;
              else if (last_line_type == modified_line && modified_end == 0)
                modified_end = hunk_text_end;
              else if (last_line_type == context_line)
                {
                  if (original_end == 0)
                    original_end = hunk_text_end;
                  if (modified_end == 0)
                    modified_end = hunk_text_end;
                }

              SVN_ERR(svn_io_file_seek(apr_file, APR_SET, &pos, iterpool));
              /* Set for the type and context by using != the other type */
              if (last_line_type != modified_line)
                original_no_final_eol = TRUE;
              if (last_line_type != original_line)
                modified_no_final_eol = TRUE;
            }

          continue;
        }

      if (in_hunk && *is_property && *prop_name &&
          strcmp(*prop_name, SVN_PROP_MERGEINFO) == 0)
        {
          svn_boolean_t found_mergeinfo;

          SVN_ERR(parse_mergeinfo(&found_mergeinfo, line, *hunk, patch,
                                  result_pool, iterpool));
          if (found_mergeinfo)
            continue; /* Proceed to the next line in the svn:mergeinfo hunk. */
          else
            {
              /* Perhaps we can also use original_lines/modified_lines here */

              in_hunk = FALSE; /* On to next property */
            }
        }

      if (in_hunk)
        {
          char c;
          static const char add = '+';
          static const char del = '-';

          if (! hunk_seen)
            {
              /* We're reading the first line of the hunk, so the start
               * of the line just read is the hunk text's byte offset. */
              start = last_line;
            }

          c = line->data[0];
          if (c == ' '
              || ((original_lines > 0 && modified_lines > 0)
                  && ( 
               /* Tolerate chopped leading spaces on empty lines. */
                      (! eof && line->len == 0)
               /* Maybe tolerate chopped leading spaces on non-empty lines. */
                      || (ignore_whitespace && c != del && c != add))))
            {
              /* It's a "context" line in the hunk. */
              hunk_seen = TRUE;
              if (original_lines > 0)
                original_lines--;
              else
                {
                  (*hunk)->original_length++;
                  (*hunk)->original_fuzz++;
                }
              if (modified_lines > 0)
                modified_lines--;
              else
                {
                  (*hunk)->modified_length++;
                  (*hunk)->modified_fuzz++;
                }
              if (changed_line_seen)
                trailing_context++;
              else
                leading_context++;
              last_line_type = context_line;
            }
          else if (c == del
                   && (original_lines > 0 || line->data[1] != del))
            {
              /* It's a "deleted" line in the hunk. */
              hunk_seen = TRUE;
              changed_line_seen = TRUE;

              /* A hunk may have context in the middle. We only want
                 trailing lines of context. */
              if (trailing_context > 0)
                trailing_context = 0;

              if (original_lines > 0)
                original_lines--;
              else
                {
                  (*hunk)->original_length++;
                  (*hunk)->original_fuzz++;
                }
              last_line_type = original_line;
            }
          else if (c == add
                   && (modified_lines > 0 || line->data[1] != add))
            {
              /* It's an "added" line in the hunk. */
              hunk_seen = TRUE;
              changed_line_seen = TRUE;

              /* A hunk may have context in the middle. We only want
                 trailing lines of context. */
              if (trailing_context > 0)
                trailing_context = 0;

              if (modified_lines > 0)
                modified_lines--;
              else
                {
                  (*hunk)->modified_length++;
                  (*hunk)->modified_fuzz++;
                }
              last_line_type = modified_line;
            }
          else
            {
              if (eof)
                {
                  /* The hunk ends at EOF. */
                  end = pos;
                }
              else
                {
                  /* The start of the current line marks the first byte
                   * after the hunk text. */
                  end = last_line;
                }
              if (original_end == 0)
                original_end = end;
              if (modified_end == 0)
                modified_end = end;
              break; /* Hunk was empty or has been read. */
            }
        }
      else
        {
          if (starts_with(line->data, text_atat))
            {
              /* Looks like we have a hunk header, try to rip it apart. */
              in_hunk = parse_hunk_header(line->data, *hunk, text_atat,
                                          iterpool);
              if (in_hunk)
                {
                  original_lines = (*hunk)->original_length;
                  modified_lines = (*hunk)->modified_length;
                  *is_property = FALSE;
                }
              }
          else if (starts_with(line->data, prop_atat))
            {
              /* Looks like we have a property hunk header, try to rip it
               * apart. */
              in_hunk = parse_hunk_header(line->data, *hunk, prop_atat,
                                          iterpool);
              if (in_hunk)
                {
                  original_lines = (*hunk)->original_length;
                  modified_lines = (*hunk)->modified_length;
                  *is_property = TRUE;
                }
            }
          else if (starts_with(line->data, "Added: "))
            {
              SVN_ERR(parse_prop_name(prop_name, line->data, "Added: ",
                                      result_pool));
              if (*prop_name)
                *prop_operation = (patch->reverse ? svn_diff_op_deleted
                                                  : svn_diff_op_added);
            }
          else if (starts_with(line->data, "Deleted: "))
            {
              SVN_ERR(parse_prop_name(prop_name, line->data, "Deleted: ",
                                      result_pool));
              if (*prop_name)
                *prop_operation = (patch->reverse ? svn_diff_op_added
                                                  : svn_diff_op_deleted);
            }
          else if (starts_with(line->data, "Modified: "))
            {
              SVN_ERR(parse_prop_name(prop_name, line->data, "Modified: ",
                                      result_pool));
              if (*prop_name)
                *prop_operation = svn_diff_op_modified;
            }
          else if (starts_with(line->data, minus)
                   || starts_with(line->data, "diff --git "))
            /* This could be a header of another patch. Bail out. */
            break;
        }
    }
  /* Check for the line length since a file may not have a newline at the
   * end and we depend upon the last line to be an empty one. */
  while (! eof || line->len > 0);
  svn_pool_destroy(iterpool);

  if (! eof)
    /* Rewind to the start of the line just read, so subsequent calls
     * to this function or svn_diff_parse_next_patch() don't end
     * up skipping the line -- it may contain a patch or hunk header. */
    SVN_ERR(svn_io_file_seek(apr_file, APR_SET, &last_line, scratch_pool));

  if (hunk_seen && start < end)
    {
      /* Did we get the number of context lines announced in the header?

         If not... let's limit the number from the header to what we
         actually have, and apply a fuzz penalty */
      if (original_lines)
        {
          (*hunk)->original_length -= original_lines;
          (*hunk)->original_fuzz += original_lines;
        }
      if (modified_lines)
        {
          (*hunk)->modified_length -= modified_lines;
          (*hunk)->modified_fuzz += modified_lines;
        }

      (*hunk)->patch = patch;
      (*hunk)->apr_file = apr_file;
      (*hunk)->leading_context = leading_context;
      (*hunk)->trailing_context = trailing_context;
      (*hunk)->diff_text_range.start = start;
      (*hunk)->diff_text_range.current = start;
      (*hunk)->diff_text_range.end = end;
      (*hunk)->original_text_range.start = start;
      (*hunk)->original_text_range.current = start;
      (*hunk)->original_text_range.end = original_end;
      (*hunk)->modified_text_range.start = start;
      (*hunk)->modified_text_range.current = start;
      (*hunk)->modified_text_range.end = modified_end;
      (*hunk)->original_no_final_eol = original_no_final_eol;
      (*hunk)->modified_no_final_eol = modified_no_final_eol;
    }
  else
    /* Something went wrong, just discard the result. */
    *hunk = NULL;

  return SVN_NO_ERROR;
}

/* Compare function for sorting hunks after parsing.
 * We sort hunks by their original line offset. */
static int
compare_hunks(const void *a, const void *b)
{
  const svn_diff_hunk_t *ha = *((const svn_diff_hunk_t *const *)a);
  const svn_diff_hunk_t *hb = *((const svn_diff_hunk_t *const *)b);

  if (ha->original_start < hb->original_start)
    return -1;
  if (ha->original_start > hb->original_start)
    return 1;
  return 0;
}

/* Possible states of the diff header parser. */
enum parse_state
{
   state_start,             /* initial */
   state_git_diff_seen,     /* diff --git */
   state_git_tree_seen,     /* a tree operation, rather than content change */
   state_git_minus_seen,    /* --- /dev/null; or --- a/ */
   state_git_plus_seen,     /* +++ /dev/null; or +++ a/ */
   state_old_mode_seen,     /* old mode 100644 */
   state_git_mode_seen,     /* new mode 100644 */
   state_move_from_seen,    /* rename from foo.c */
   state_copy_from_seen,    /* copy from foo.c */
   state_minus_seen,        /* --- foo.c */
   state_unidiff_found,     /* valid start of a regular unidiff header */
   state_git_header_found,  /* valid start of a --git diff header */
   state_binary_patch_found /* valid start of binary patch */
};

/* Data type describing a valid state transition of the parser. */
struct transition
{
  const char *expected_input;
  enum parse_state required_state;

  /* A callback called upon each parser state transition. */
  svn_error_t *(*fn)(enum parse_state *new_state, char *input,
                     svn_patch_t *patch, apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool);
};

/* UTF-8 encode and canonicalize the content of LINE as FILE_NAME. */
static svn_error_t *
grab_filename(const char **file_name, const char *line, apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  const char *utf8_path;
  const char *canon_path;

  /* Grab the filename and encode it in UTF-8. */
  /* TODO: Allow specifying the patch file's encoding.
   *       For now, we assume its encoding is native. */
  /* ### This can fail if the filename cannot be represented in the current
   * ### locale's encoding. */
  SVN_ERR(svn_utf_cstring_to_utf8(&utf8_path,
                                  line,
                                  scratch_pool));

  /* Canonicalize the path name. */
  canon_path = svn_dirent_canonicalize(utf8_path, scratch_pool);

  *file_name = apr_pstrdup(result_pool, canon_path);

  return SVN_NO_ERROR;
}

/* Parse the '--- ' line of a regular unidiff. */
static svn_error_t *
diff_minus(enum parse_state *new_state, char *line, svn_patch_t *patch,
           apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  /* If we can find a tab, it separates the filename from
   * the rest of the line which we can discard. */
  char *tab = strchr(line, '\t');
  if (tab)
    *tab = '\0';

  SVN_ERR(grab_filename(&patch->old_filename, line + STRLEN_LITERAL("--- "),
                        result_pool, scratch_pool));

  *new_state = state_minus_seen;

  return SVN_NO_ERROR;
}

/* Parse the '+++ ' line of a regular unidiff. */
static svn_error_t *
diff_plus(enum parse_state *new_state, char *line, svn_patch_t *patch,
           apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  /* If we can find a tab, it separates the filename from
   * the rest of the line which we can discard. */
  char *tab = strchr(line, '\t');
  if (tab)
    *tab = '\0';

  SVN_ERR(grab_filename(&patch->new_filename, line + STRLEN_LITERAL("+++ "),
                        result_pool, scratch_pool));

  *new_state = state_unidiff_found;

  return SVN_NO_ERROR;
}

/* Parse the first line of a git extended unidiff. */
static svn_error_t *
git_start(enum parse_state *new_state, char *line, svn_patch_t *patch,
          apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  const char *old_path_start;
  char *old_path_end;
  const char *new_path_start;
  const char *new_path_end;
  char *new_path_marker;
  const char *old_path_marker;

  /* ### Add handling of escaped paths
   * http://www.kernel.org/pub/software/scm/git/docs/git-diff.html:
   *
   * TAB, LF, double quote and backslash characters in pathnames are
   * represented as \t, \n, \" and \\, respectively. If there is need for
   * such substitution then the whole pathname is put in double quotes.
   */

  /* Our line should look like this: 'diff --git a/path b/path'.
   *
   * If we find any deviations from that format, we return with state reset
   * to start.
   */
  old_path_marker = strstr(line, " a/");

  if (! old_path_marker)
    {
      *new_state = state_start;
      return SVN_NO_ERROR;
    }

  if (! *(old_path_marker + 3))
    {
      *new_state = state_start;
      return SVN_NO_ERROR;
    }

  new_path_marker = strstr(old_path_marker, " b/");

  if (! new_path_marker)
    {
      *new_state = state_start;
      return SVN_NO_ERROR;
    }

  if (! *(new_path_marker + 3))
    {
      *new_state = state_start;
      return SVN_NO_ERROR;
    }

  /* By now, we know that we have a line on the form '--git diff a/.+ b/.+'
   * We only need the filenames when we have deleted or added empty
   * files. In those cases the old_path and new_path is identical on the
   * 'diff --git' line.  For all other cases we fetch the filenames from
   * other header lines. */
  old_path_start = line + STRLEN_LITERAL("diff --git a/");
  new_path_end = line + strlen(line);
  new_path_start = old_path_start;

  while (TRUE)
    {
      ptrdiff_t len_old;
      ptrdiff_t len_new;

      new_path_marker = strstr(new_path_start, " b/");

      /* No new path marker, bail out. */
      if (! new_path_marker)
        break;

      old_path_end = new_path_marker;
      new_path_start = new_path_marker + STRLEN_LITERAL(" b/");

      /* No path after the marker. */
      if (! *new_path_start)
        break;

      len_old = old_path_end - old_path_start;
      len_new = new_path_end - new_path_start;

      /* Are the paths before and after the " b/" marker the same? */
      if (len_old == len_new
          && ! strncmp(old_path_start, new_path_start, len_old))
        {
          *old_path_end = '\0';
          SVN_ERR(grab_filename(&patch->old_filename, old_path_start,
                                result_pool, scratch_pool));

          SVN_ERR(grab_filename(&patch->new_filename, new_path_start,
                                result_pool, scratch_pool));
          break;
        }
    }

  /* We assume that the path is only modified until we've found a 'tree'
   * header */
  patch->operation = svn_diff_op_modified;

  *new_state = state_git_diff_seen;
  return SVN_NO_ERROR;
}

/* Parse the '--- ' line of a git extended unidiff. */
static svn_error_t *
git_minus(enum parse_state *new_state, char *line, svn_patch_t *patch,
          apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  /* If we can find a tab, it separates the filename from
   * the rest of the line which we can discard. */
  char *tab = strchr(line, '\t');
  if (tab)
    *tab = '\0';

  if (starts_with(line, "--- /dev/null"))
    SVN_ERR(grab_filename(&patch->old_filename, "/dev/null",
                          result_pool, scratch_pool));
  else
    SVN_ERR(grab_filename(&patch->old_filename, line + STRLEN_LITERAL("--- a/"),
                          result_pool, scratch_pool));

  *new_state = state_git_minus_seen;
  return SVN_NO_ERROR;
}

/* Parse the '+++ ' line of a git extended unidiff. */
static svn_error_t *
git_plus(enum parse_state *new_state, char *line, svn_patch_t *patch,
          apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  /* If we can find a tab, it separates the filename from
   * the rest of the line which we can discard. */
  char *tab = strchr(line, '\t');
  if (tab)
    *tab = '\0';

  if (starts_with(line, "+++ /dev/null"))
    SVN_ERR(grab_filename(&patch->new_filename, "/dev/null",
                          result_pool, scratch_pool));
  else
    SVN_ERR(grab_filename(&patch->new_filename, line + STRLEN_LITERAL("+++ b/"),
                          result_pool, scratch_pool));

  *new_state = state_git_header_found;
  return SVN_NO_ERROR;
}

/* Helper for git_old_mode() and git_new_mode().  Translate the git
 * file mode MODE_STR into a binary "executable?" and "symlink?" state. */
static svn_error_t *
parse_git_mode_bits(svn_tristate_t *executable_p,
                    svn_tristate_t *symlink_p,
                    const char *mode_str)
{
  apr_uint64_t mode;
  SVN_ERR(svn_cstring_strtoui64(&mode, mode_str,
                                0 /* min */,
                                0777777 /* max: six octal digits */,
                                010 /* radix (octal) */));

  /* Note: 0644 and 0755 are the only modes that can occur for plain files.
   * We deliberately choose to parse only those values: we are strict in what
   * we accept _and_ in what we produce.
   *
   * (Having said that, though, we could consider relaxing the parser to also
   * map
   *     (mode & 0111) == 0000 -> svn_tristate_false
   *     (mode & 0111) == 0111 -> svn_tristate_true
   *        [anything else]    -> svn_tristate_unknown
   * .)
   */

  switch (mode & 0777)
    {
      case 0644:
        *executable_p = svn_tristate_false;
        break;

      case 0755:
        *executable_p = svn_tristate_true;
        break;

      default:
        /* Ignore unknown values. */
        *executable_p = svn_tristate_unknown;
        break;
    }

  switch (mode & 0170000 /* S_IFMT */)
    {
      case 0120000: /* S_IFLNK */
        *symlink_p = svn_tristate_true;
        break;

      case 0100000: /* S_IFREG */
      case 0040000: /* S_IFDIR */
        *symlink_p = svn_tristate_false;
        break;

      default:
        /* Ignore unknown values.
           (Including those generated by Subversion <= 1.9) */
        *symlink_p = svn_tristate_unknown;
        break;
    }

  return SVN_NO_ERROR;
}

/* Parse the 'old mode ' line of a git extended unidiff. */
static svn_error_t *
git_old_mode(enum parse_state *new_state, char *line, svn_patch_t *patch,
             apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  SVN_ERR(parse_git_mode_bits(&patch->old_executable_bit,
                              &patch->old_symlink_bit,
                              line + STRLEN_LITERAL("old mode ")));

#ifdef SVN_DEBUG
  /* If this assert trips, the "old mode" is neither ...644 nor ...755 . */
  SVN_ERR_ASSERT(patch->old_executable_bit != svn_tristate_unknown);
#endif

  *new_state = state_old_mode_seen;
  return SVN_NO_ERROR;
}

/* Parse the 'new mode ' line of a git extended unidiff. */
static svn_error_t *
git_new_mode(enum parse_state *new_state, char *line, svn_patch_t *patch,
             apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  SVN_ERR(parse_git_mode_bits(&patch->new_executable_bit,
                              &patch->new_symlink_bit,
                              line + STRLEN_LITERAL("new mode ")));

#ifdef SVN_DEBUG
  /* If this assert trips, the "old mode" is neither ...644 nor ...755 . */
  SVN_ERR_ASSERT(patch->new_executable_bit != svn_tristate_unknown);
#endif

  /* Don't touch patch->operation. */

  *new_state = state_git_mode_seen;
  return SVN_NO_ERROR;
}

static svn_error_t *
git_index(enum parse_state *new_state, char *line, svn_patch_t *patch,
          apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  /* We either have something like "index 33e5b38..0000000" (which we just
     ignore as we are not interested in git specific shas) or something like
     "index 33e5b38..0000000 120000" which tells us the mode, that isn't
     changed by applying this patch.

     If the mode would have changed then we would see 'old mode' and 'new mode'
     lines.
  */
  line = strchr(line + STRLEN_LITERAL("index "), ' ');

  if (line && patch->new_executable_bit == svn_tristate_unknown
           && patch->new_symlink_bit == svn_tristate_unknown
           && patch->operation != svn_diff_op_added
           && patch->operation != svn_diff_op_deleted)
    {
      SVN_ERR(parse_git_mode_bits(&patch->new_executable_bit,
                                  &patch->new_symlink_bit,
                                  line + 1));

      /* There is no change.. so set the old values to the new values */
      patch->old_executable_bit = patch->new_executable_bit;
      patch->old_symlink_bit = patch->new_symlink_bit;
    }

  /* This function doesn't change the state! */
  /* *new_state = *new_state */
  return SVN_NO_ERROR;
}

/* Parse the 'rename from ' line of a git extended unidiff. */
static svn_error_t *
git_move_from(enum parse_state *new_state, char *line, svn_patch_t *patch,
              apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  SVN_ERR(grab_filename(&patch->old_filename,
                        line + STRLEN_LITERAL("rename from "),
                        result_pool, scratch_pool));

  *new_state = state_move_from_seen;
  return SVN_NO_ERROR;
}

/* Parse the 'rename to ' line of a git extended unidiff. */
static svn_error_t *
git_move_to(enum parse_state *new_state, char *line, svn_patch_t *patch,
            apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  SVN_ERR(grab_filename(&patch->new_filename,
                        line + STRLEN_LITERAL("rename to "),
                        result_pool, scratch_pool));

  patch->operation = svn_diff_op_moved;

  *new_state = state_git_tree_seen;
  return SVN_NO_ERROR;
}

/* Parse the 'copy from ' line of a git extended unidiff. */
static svn_error_t *
git_copy_from(enum parse_state *new_state, char *line, svn_patch_t *patch,
              apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  SVN_ERR(grab_filename(&patch->old_filename,
                        line + STRLEN_LITERAL("copy from "),
                        result_pool, scratch_pool));

  *new_state = state_copy_from_seen;
  return SVN_NO_ERROR;
}

/* Parse the 'copy to ' line of a git extended unidiff. */
static svn_error_t *
git_copy_to(enum parse_state *new_state, char *line, svn_patch_t *patch,
            apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  SVN_ERR(grab_filename(&patch->new_filename, line + STRLEN_LITERAL("copy to "),
                        result_pool, scratch_pool));

  patch->operation = svn_diff_op_copied;

  *new_state = state_git_tree_seen;
  return SVN_NO_ERROR;
}

/* Parse the 'new file ' line of a git extended unidiff. */
static svn_error_t *
git_new_file(enum parse_state *new_state, char *line, svn_patch_t *patch,
             apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  SVN_ERR(parse_git_mode_bits(&patch->new_executable_bit,
                              &patch->new_symlink_bit,
                              line + STRLEN_LITERAL("new file mode ")));

  patch->operation = svn_diff_op_added;

  /* Filename already retrieved from diff --git header. */

  *new_state = state_git_tree_seen;
  return SVN_NO_ERROR;
}

/* Parse the 'deleted file ' line of a git extended unidiff. */
static svn_error_t *
git_deleted_file(enum parse_state *new_state, char *line, svn_patch_t *patch,
                 apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  SVN_ERR(parse_git_mode_bits(&patch->old_executable_bit,
                              &patch->old_symlink_bit,
                              line + STRLEN_LITERAL("deleted file mode ")));

  patch->operation = svn_diff_op_deleted;

  /* Filename already retrieved from diff --git header. */

  *new_state = state_git_tree_seen;
  return SVN_NO_ERROR;
}

/* Parse the 'GIT binary patch' header */
static svn_error_t *
binary_patch_start(enum parse_state *new_state, char *line, svn_patch_t *patch,
             apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  *new_state = state_binary_patch_found;
  return SVN_NO_ERROR;
}


/* Add a HUNK associated with the property PROP_NAME to PATCH. */
static svn_error_t *
add_property_hunk(svn_patch_t *patch, const char *prop_name,
                  svn_diff_hunk_t *hunk, svn_diff_operation_kind_t operation,
                  apr_pool_t *result_pool)
{
  svn_prop_patch_t *prop_patch;

  prop_patch = svn_hash_gets(patch->prop_patches, prop_name);

  if (! prop_patch)
    {
      prop_patch = apr_palloc(result_pool, sizeof(svn_prop_patch_t));
      prop_patch->name = prop_name;
      prop_patch->operation = operation;
      prop_patch->hunks = apr_array_make(result_pool, 1,
                                         sizeof(svn_diff_hunk_t *));

      svn_hash_sets(patch->prop_patches, prop_name, prop_patch);
    }

  APR_ARRAY_PUSH(prop_patch->hunks, svn_diff_hunk_t *) = hunk;

  return SVN_NO_ERROR;
}

struct svn_patch_file_t
{
  /* The APR file handle to the patch file. */
  apr_file_t *apr_file;

  /* The file offset at which the next patch is expected. */
  apr_off_t next_patch_offset;
};

svn_error_t *
svn_diff_open_patch_file(svn_patch_file_t **patch_file,
                         const char *local_abspath,
                         apr_pool_t *result_pool)
{
  svn_patch_file_t *p;

  p = apr_palloc(result_pool, sizeof(*p));
  SVN_ERR(svn_io_file_open(&p->apr_file, local_abspath,
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT,
                           result_pool));
  p->next_patch_offset = 0;
  *patch_file = p;

  return SVN_NO_ERROR;
}

/* Parse hunks from APR_FILE and store them in PATCH->HUNKS.
 * Parsing stops if no valid next hunk can be found.
 * If IGNORE_WHITESPACE is TRUE, lines without
 * leading spaces will be treated as context lines.
 * Allocate results in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
parse_hunks(svn_patch_t *patch, apr_file_t *apr_file,
            svn_boolean_t ignore_whitespace,
            apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  svn_diff_hunk_t *hunk;
  svn_boolean_t is_property;
  const char *last_prop_name;
  const char *prop_name;
  svn_diff_operation_kind_t prop_operation;
  apr_pool_t *iterpool;

  last_prop_name = NULL;

  patch->hunks = apr_array_make(result_pool, 10, sizeof(svn_diff_hunk_t *));
  patch->prop_patches = apr_hash_make(result_pool);
  iterpool = svn_pool_create(scratch_pool);
  do
    {
      svn_pool_clear(iterpool);

      SVN_ERR(parse_next_hunk(&hunk, &is_property, &prop_name, &prop_operation,
                              patch, apr_file, ignore_whitespace, result_pool,
                              iterpool));

      if (hunk && is_property)
        {
          if (! prop_name)
            prop_name = last_prop_name;
          else
            last_prop_name = prop_name;

          /* Skip svn:mergeinfo properties.
           * Mergeinfo data cannot be represented as a hunk and
           * is therefore stored in PATCH itself. */
          if (strcmp(prop_name, SVN_PROP_MERGEINFO) == 0)
            continue;

          SVN_ERR(add_property_hunk(patch, prop_name, hunk, prop_operation,
                                    result_pool));
        }
      else if (hunk)
        {
          APR_ARRAY_PUSH(patch->hunks, svn_diff_hunk_t *) = hunk;
          last_prop_name = NULL;
        }

    }
  while (hunk);
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
parse_binary_patch(svn_patch_t *patch, apr_file_t *apr_file,
                   svn_boolean_t reverse,
                   apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_off_t pos, last_line;
  svn_stringbuf_t *line;
  svn_boolean_t eof = FALSE;
  svn_diff_binary_patch_t *bpatch = apr_pcalloc(result_pool, sizeof(*bpatch));
  svn_boolean_t in_blob = FALSE;
  svn_boolean_t in_src = FALSE;

  bpatch->apr_file = apr_file;

  patch->prop_patches = apr_hash_make(result_pool);

  SVN_ERR(svn_io_file_get_offset(&pos, apr_file, scratch_pool));

  while (!eof)
    {
      last_line = pos;
      SVN_ERR(svn_io_file_readline(apr_file, &line, NULL, &eof, APR_SIZE_MAX,
                               iterpool, iterpool));

      /* Update line offset for next iteration. */
      SVN_ERR(svn_io_file_get_offset(&pos, apr_file, iterpool));

      if (in_blob)
        {
          char c = line->data[0];

          /* 66 = len byte + (52/4*5) chars */
          if (((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
              && line->len <= 66
              && !strchr(line->data, ':')
              && !strchr(line->data, ' '))
            {
              /* One more blop line */
              if (in_src)
                bpatch->src_end = pos;
              else
                bpatch->dst_end = pos;
            }
          else if (svn_stringbuf_first_non_whitespace(line) < line->len
                   && !(in_src && bpatch->src_start < last_line))
            {
              break; /* Bad patch */
            }
          else if (in_src)
            {
              patch->binary_patch = bpatch; /* SUCCESS! */
              break; 
            }
          else
            {
              in_blob = FALSE;
              in_src = TRUE;
            }
        }
      else if (starts_with(line->data, "literal "))
        {
          apr_uint64_t expanded_size;
          svn_error_t *err = svn_cstring_strtoui64(&expanded_size,
                                                   &line->data[8],
                                                   0, APR_UINT64_MAX, 10);

          if (err)
            {
              svn_error_clear(err);
              break;
            }

          if (in_src)
            {
              bpatch->src_start = pos;
              bpatch->src_filesize = expanded_size;
            }
          else
            {
              bpatch->dst_start = pos;
              bpatch->dst_filesize = expanded_size;
            }
          in_blob = TRUE;
        }
      else
        break; /* We don't support GIT deltas (yet) */
    }
  svn_pool_destroy(iterpool);

  if (!eof)
    /* Rewind to the start of the line just read, so subsequent calls
     * don't end up skipping the line. It may contain a patch or hunk header.*/
    SVN_ERR(svn_io_file_seek(apr_file, APR_SET, &last_line, scratch_pool));
  else if (in_src
           && ((bpatch->src_end > bpatch->src_start) || !bpatch->src_filesize))
    {
      patch->binary_patch = bpatch; /* SUCCESS */
    }

  /* Reverse patch if requested */
  if (reverse && patch->binary_patch)
    {
      apr_off_t tmp_start = bpatch->src_start;
      apr_off_t tmp_end = bpatch->src_end;
      svn_filesize_t tmp_filesize = bpatch->src_filesize;

      bpatch->src_start = bpatch->dst_start;
      bpatch->src_end = bpatch->dst_end;
      bpatch->src_filesize = bpatch->dst_filesize;

      bpatch->dst_start = tmp_start;
      bpatch->dst_end = tmp_end;
      bpatch->dst_filesize = tmp_filesize;
    }

  return SVN_NO_ERROR;
}

/* State machine for the diff header parser.
 * Expected Input   Required state          Function to call */
static struct transition transitions[] =
{
  {"--- ",              state_start,            diff_minus},
  {"+++ ",              state_minus_seen,       diff_plus},

  {"diff --git",        state_start,            git_start},
  {"--- a/",            state_git_diff_seen,    git_minus},
  {"--- a/",            state_git_mode_seen,    git_minus},
  {"--- a/",            state_git_tree_seen,    git_minus},
  {"--- /dev/null",     state_git_mode_seen,    git_minus},
  {"--- /dev/null",     state_git_tree_seen,    git_minus},
  {"+++ b/",            state_git_minus_seen,   git_plus},
  {"+++ /dev/null",     state_git_minus_seen,   git_plus},

  {"old mode ",         state_git_diff_seen,    git_old_mode},
  {"new mode ",         state_old_mode_seen,    git_new_mode},

  {"rename from ",      state_git_diff_seen,    git_move_from},
  {"rename from ",      state_git_mode_seen,    git_move_from},
  {"rename to ",        state_move_from_seen,   git_move_to},

  {"copy from ",        state_git_diff_seen,    git_copy_from},
  {"copy from ",        state_git_mode_seen,    git_copy_from},
  {"copy to ",          state_copy_from_seen,   git_copy_to},

  {"new file ",         state_git_diff_seen,    git_new_file},

  {"deleted file ",     state_git_diff_seen,    git_deleted_file},

  {"index ",            state_git_diff_seen,    git_index},
  {"index ",            state_git_tree_seen,    git_index},
  {"index ",            state_git_mode_seen,    git_index},

  {"GIT binary patch",  state_git_diff_seen,    binary_patch_start},
  {"GIT binary patch",  state_git_tree_seen,    binary_patch_start},
  {"GIT binary patch",  state_git_mode_seen,    binary_patch_start},
};

svn_error_t *
svn_diff_parse_next_patch(svn_patch_t **patch_p,
                          svn_patch_file_t *patch_file,
                          svn_boolean_t reverse,
                          svn_boolean_t ignore_whitespace,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  apr_off_t pos, last_line;
  svn_boolean_t eof;
  svn_boolean_t line_after_tree_header_read = FALSE;
  apr_pool_t *iterpool;
  svn_patch_t *patch;
  enum parse_state state = state_start;

  if (apr_file_eof(patch_file->apr_file) == APR_EOF)
    {
      /* No more patches here. */
      *patch_p = NULL;
      return SVN_NO_ERROR;
    }

  patch = apr_pcalloc(result_pool, sizeof(*patch));
  patch->old_executable_bit = svn_tristate_unknown;
  patch->new_executable_bit = svn_tristate_unknown;
  patch->old_symlink_bit = svn_tristate_unknown;
  patch->new_symlink_bit = svn_tristate_unknown;

  pos = patch_file->next_patch_offset;
  SVN_ERR(svn_io_file_seek(patch_file->apr_file, APR_SET, &pos, scratch_pool));

  iterpool = svn_pool_create(scratch_pool);
  do
    {
      svn_stringbuf_t *line;
      svn_boolean_t valid_header_line = FALSE;
      int i;

      svn_pool_clear(iterpool);

      /* Remember the current line's offset, and read the line. */
      last_line = pos;
      SVN_ERR(svn_io_file_readline(patch_file->apr_file, &line, NULL, &eof,
                                   APR_SIZE_MAX, iterpool, iterpool));

      if (! eof)
        {
          /* Update line offset for next iteration. */
          SVN_ERR(svn_io_file_get_offset(&pos, patch_file->apr_file,
                                         iterpool));
        }

      /* Run the state machine. */
      for (i = 0; i < (sizeof(transitions) / sizeof(transitions[0])); i++)
        {
          if (starts_with(line->data, transitions[i].expected_input)
              && state == transitions[i].required_state)
            {
              SVN_ERR(transitions[i].fn(&state, line->data, patch,
                                        result_pool, iterpool));
              valid_header_line = TRUE;
              break;
            }
        }

      if (state == state_unidiff_found
          || state == state_git_header_found
          || state == state_binary_patch_found)
        {
          /* We have a valid diff header, yay! */
          break;
        }
      else if ((state == state_git_tree_seen || state == state_git_mode_seen)
               && line_after_tree_header_read
               && !valid_header_line)
        {
          /* We have a valid diff header for a patch with only tree changes.
           * Rewind to the start of the line just read, so subsequent calls
           * to this function don't end up skipping the line -- it may
           * contain a patch. */
          SVN_ERR(svn_io_file_seek(patch_file->apr_file, APR_SET, &last_line,
                                   scratch_pool));
          break;
        }
      else if (state == state_git_tree_seen
               || state == state_git_mode_seen)
        {
          line_after_tree_header_read = TRUE;
        }
      else if (! valid_header_line && state != state_start
               && state != state_git_diff_seen)
        {
          /* We've encountered an invalid diff header.
           *
           * Rewind to the start of the line just read - it may be a new
           * header that begins there. */
          SVN_ERR(svn_io_file_seek(patch_file->apr_file, APR_SET, &last_line,
                                   scratch_pool));
          state = state_start;
        }

    }
  while (! eof);

  patch->reverse = reverse;
  if (reverse)
    {
      const char *temp;
      svn_tristate_t ts_tmp;

      temp = patch->old_filename;
      patch->old_filename = patch->new_filename;
      patch->new_filename = temp;

      switch (patch->operation)
        {
          case svn_diff_op_added:
            patch->operation = svn_diff_op_deleted;
            break;
          case svn_diff_op_deleted:
            patch->operation = svn_diff_op_added;
            break;

          case svn_diff_op_modified:
            break; /* Stays modified. */

          case svn_diff_op_copied:
          case svn_diff_op_moved:
            break; /* Stays copied or moved, just in the other direction. */
          case svn_diff_op_unchanged:
            break; /* Stays unchanged, of course. */
        }

      ts_tmp = patch->old_executable_bit;
      patch->old_executable_bit = patch->new_executable_bit;
      patch->new_executable_bit = ts_tmp;

      ts_tmp = patch->old_symlink_bit;
      patch->old_symlink_bit = patch->new_symlink_bit;
      patch->new_symlink_bit = ts_tmp;
    }

  if (patch->old_filename == NULL || patch->new_filename == NULL)
    {
      /* Something went wrong, just discard the result. */
      patch = NULL;
    }
  else
    {
      if (state == state_binary_patch_found)
        {
          SVN_ERR(parse_binary_patch(patch, patch_file->apr_file, reverse,
                                     result_pool, iterpool));
          /* And fall through in property parsing */
        }

      SVN_ERR(parse_hunks(patch, patch_file->apr_file, ignore_whitespace,
                          result_pool, iterpool));
    }

  svn_pool_destroy(iterpool);

  SVN_ERR(svn_io_file_get_offset(&patch_file->next_patch_offset,
                                 patch_file->apr_file, scratch_pool));

  if (patch && patch->hunks)
    {
      /* Usually, hunks appear in the patch sorted by their original line
       * offset. But just in case they weren't parsed in this order for
       * some reason, we sort them so that our caller can assume that hunks
       * are sorted as if parsed from a usual patch. */
      svn_sort__array(patch->hunks, compare_hunks);
    }

  *patch_p = patch;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff_close_patch_file(svn_patch_file_t *patch_file,
                          apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_io_file_close(patch_file->apr_file,
                                           scratch_pool));
}
