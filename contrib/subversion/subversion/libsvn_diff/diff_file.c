/*
 * diff_file.c :  routines for doing diffs on files
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


#include <apr.h>
#include <apr_pools.h>
#include <apr_general.h>
#include <apr_file_io.h>
#include <apr_file_info.h>
#include <apr_time.h>
#include <apr_mmap.h>
#include <apr_getopt.h>

#include <assert.h>

#include "svn_error.h"
#include "svn_diff.h"
#include "svn_types.h"
#include "svn_string.h"
#include "svn_subst.h"
#include "svn_io.h"
#include "svn_utf.h"
#include "svn_pools.h"
#include "diff.h"
#include "svn_private_config.h"
#include "svn_path.h"
#include "svn_ctype.h"

#include "private/svn_utf_private.h"
#include "private/svn_eol_private.h"
#include "private/svn_dep_compat.h"
#include "private/svn_adler32.h"
#include "private/svn_diff_private.h"

/* A token, i.e. a line read from a file. */
typedef struct svn_diff__file_token_t
{
  /* Next token in free list. */
  struct svn_diff__file_token_t *next;
  svn_diff_datasource_e datasource;
  /* Offset in the datasource. */
  apr_off_t offset;
  /* Offset of the normalized token (may skip leading whitespace) */
  apr_off_t norm_offset;
  /* Total length - before normalization. */
  apr_off_t raw_length;
  /* Total length - after normalization. */
  apr_off_t length;
} svn_diff__file_token_t;


typedef struct svn_diff__file_baton_t
{
  const svn_diff_file_options_t *options;

  struct file_info {
    const char *path;  /* path to this file, absolute or relative to CWD */

    /* All the following fields are active while this datasource is open */
    apr_file_t *file;  /* handle of this file */
    apr_off_t size;    /* total raw size in bytes of this file */

    /* The current chunk: CHUNK_SIZE bytes except for the last chunk. */
    int chunk;     /* the current chunk number, zero-based */
    char *buffer;  /* a buffer containing the current chunk */
    char *curp;    /* current position in the current chunk */
    char *endp;    /* next memory address after the current chunk */

    svn_diff__normalize_state_t normalize_state;

    /* Where the identical suffix starts in this datasource */
    int suffix_start_chunk;
    apr_off_t suffix_offset_in_chunk;
  } files[4];

  /* List of free tokens that may be reused. */
  svn_diff__file_token_t *tokens;

  apr_pool_t *pool;
} svn_diff__file_baton_t;

static int
datasource_to_index(svn_diff_datasource_e datasource)
{
  switch (datasource)
    {
    case svn_diff_datasource_original:
      return 0;

    case svn_diff_datasource_modified:
      return 1;

    case svn_diff_datasource_latest:
      return 2;

    case svn_diff_datasource_ancestor:
      return 3;
    }

  return -1;
}

/* Files are read in chunks of 128k.  There is no support for this number
 * whatsoever.  If there is a number someone comes up with that has some
 * argumentation, let's use that.
 */
/* If you change this number, update test_norm_offset(),
 * test_identical_suffix() and and test_token_compare()  in diff-diff3-test.c.
 */
#define CHUNK_SHIFT 17
#define CHUNK_SIZE (1 << CHUNK_SHIFT)

#define chunk_to_offset(chunk) ((chunk) << CHUNK_SHIFT)
#define offset_to_chunk(offset) ((offset) >> CHUNK_SHIFT)
#define offset_in_chunk(offset) ((offset) & (CHUNK_SIZE - 1))


/* Read a chunk from a FILE into BUFFER, starting from OFFSET, going for
 * *LENGTH.  The actual bytes read are stored in *LENGTH on return.
 */
static APR_INLINE svn_error_t *
read_chunk(apr_file_t *file,
           char *buffer, apr_off_t length,
           apr_off_t offset, apr_pool_t *scratch_pool)
{
  /* XXX: The final offset may not be the one we asked for.
   * XXX: Check.
   */
  SVN_ERR(svn_io_file_seek(file, APR_SET, &offset, scratch_pool));
  return svn_io_file_read_full2(file, buffer, (apr_size_t) length,
                                NULL, NULL, scratch_pool);
}


/* Map or read a file at PATH. *BUFFER will point to the file
 * contents; if the file was mapped, *FILE and *MM will contain the
 * mmap context; otherwise they will be NULL.  SIZE will contain the
 * file size.  Allocate from POOL.
 */
#if APR_HAS_MMAP
#define MMAP_T_PARAM(NAME) apr_mmap_t **NAME,
#define MMAP_T_ARG(NAME)   &(NAME),
#else
#define MMAP_T_PARAM(NAME)
#define MMAP_T_ARG(NAME)
#endif

static svn_error_t *
map_or_read_file(apr_file_t **file,
                 MMAP_T_PARAM(mm)
                 char **buffer, apr_size_t *size_p,
                 const char *path, apr_pool_t *pool)
{
  apr_finfo_t finfo;
  apr_status_t rv;
  apr_size_t size;

  *buffer = NULL;

  SVN_ERR(svn_io_file_open(file, path, APR_READ, APR_OS_DEFAULT, pool));
  SVN_ERR(svn_io_file_info_get(&finfo, APR_FINFO_SIZE, *file, pool));

  if (finfo.size > APR_SIZE_MAX)
    {
      return svn_error_createf(APR_ENOMEM, NULL,
                               _("File '%s' is too large to be read in "
                                 "to memory"), path);
    }

  size = (apr_size_t) finfo.size;
#if APR_HAS_MMAP
  if (size > APR_MMAP_THRESHOLD)
    {
      rv = apr_mmap_create(mm, *file, 0, size, APR_MMAP_READ, pool);
      if (rv == APR_SUCCESS)
        {
          *buffer = (*mm)->mm;
        }
      else
        {
          /* Clear *MM because output parameters are undefined on error. */
          *mm = NULL;
        }

      /* On failure we just fall through and try reading the file into
       * memory instead.
       */
    }
#endif /* APR_HAS_MMAP */

   if (*buffer == NULL && size > 0)
    {
      *buffer = apr_palloc(pool, size);

      SVN_ERR(svn_io_file_read_full2(*file, *buffer, size, NULL, NULL, pool));

      /* Since we have the entire contents of the file we can
       * close it now.
       */
      SVN_ERR(svn_io_file_close(*file, pool));

      *file = NULL;
    }

  *size_p = size;

  return SVN_NO_ERROR;
}


/* For all files in the FILE array, increment the curp pointer.  If a file
 * points before the beginning of file, let it point at the first byte again.
 * If the end of the current chunk is reached, read the next chunk in the
 * buffer and point curp to the start of the chunk.  If EOF is reached, set
 * curp equal to endp to indicate EOF. */
#define INCREMENT_POINTERS(all_files, files_len, pool)                       \
  do {                                                                       \
    apr_size_t svn_macro__i;                                                 \
                                                                             \
    for (svn_macro__i = 0; svn_macro__i < (files_len); svn_macro__i++)       \
    {                                                                        \
      if ((all_files)[svn_macro__i].curp < (all_files)[svn_macro__i].endp - 1)\
        (all_files)[svn_macro__i].curp++;                                    \
      else                                                                   \
        SVN_ERR(increment_chunk(&(all_files)[svn_macro__i], (pool)));        \
    }                                                                        \
  } while (0)


/* For all files in the FILE array, decrement the curp pointer.  If the
 * start of a chunk is reached, read the previous chunk in the buffer and
 * point curp to the last byte of the chunk.  If the beginning of a FILE is
 * reached, set chunk to -1 to indicate BOF. */
#define DECREMENT_POINTERS(all_files, files_len, pool)                       \
  do {                                                                       \
    apr_size_t svn_macro__i;                                                 \
                                                                             \
    for (svn_macro__i = 0; svn_macro__i < (files_len); svn_macro__i++)       \
    {                                                                        \
      if ((all_files)[svn_macro__i].curp > (all_files)[svn_macro__i].buffer) \
        (all_files)[svn_macro__i].curp--;                                    \
      else                                                                   \
        SVN_ERR(decrement_chunk(&(all_files)[svn_macro__i], (pool)));        \
    }                                                                        \
  } while (0)


static svn_error_t *
increment_chunk(struct file_info *file, apr_pool_t *pool)
{
  apr_off_t length;
  apr_off_t last_chunk = offset_to_chunk(file->size);

  if (file->chunk == -1)
    {
      /* We are at BOF (Beginning Of File). Point to first chunk/byte again. */
      file->chunk = 0;
      file->curp = file->buffer;
    }
  else if (file->chunk == last_chunk)
    {
      /* We are at the last chunk. Indicate EOF by setting curp == endp. */
      file->curp = file->endp;
    }
  else
    {
      /* There are still chunks left. Read next chunk and reset pointers. */
      file->chunk++;
      length = file->chunk == last_chunk ?
        offset_in_chunk(file->size) : CHUNK_SIZE;
      SVN_ERR(read_chunk(file->file, file->buffer,
                         length, chunk_to_offset(file->chunk),
                         pool));
      file->endp = file->buffer + length;
      file->curp = file->buffer;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
decrement_chunk(struct file_info *file, apr_pool_t *pool)
{
  if (file->chunk == 0)
    {
      /* We are already at the first chunk. Indicate BOF (Beginning Of File)
         by setting chunk = -1 and curp = endp - 1. Both conditions are
         important. They help the increment step to catch the BOF situation
         in an efficient way. */
      file->chunk--;
      file->curp = file->endp - 1;
    }
  else
    {
      /* Read previous chunk and reset pointers. */
      file->chunk--;
      SVN_ERR(read_chunk(file->file, file->buffer,
                         CHUNK_SIZE, chunk_to_offset(file->chunk),
                         pool));
      file->endp = file->buffer + CHUNK_SIZE;
      file->curp = file->endp - 1;
    }

  return SVN_NO_ERROR;
}


/* Check whether one of the FILEs has its pointers 'before' the beginning of
 * the file (this can happen while scanning backwards). This is the case if
 * one of them has chunk == -1. */
static svn_boolean_t
is_one_at_bof(struct file_info file[], apr_size_t file_len)
{
  apr_size_t i;

  for (i = 0; i < file_len; i++)
    if (file[i].chunk == -1)
      return TRUE;

  return FALSE;
}

/* Check whether one of the FILEs has its pointers at EOF (this is the case if
 * one of them has curp == endp (this can only happen at the last chunk)) */
static svn_boolean_t
is_one_at_eof(struct file_info file[], apr_size_t file_len)
{
  apr_size_t i;

  for (i = 0; i < file_len; i++)
    if (file[i].curp == file[i].endp)
      return TRUE;

  return FALSE;
}

/* Quickly determine whether there is a eol char in CHUNK.
 * (mainly copy-n-paste from eol.c#svn_eol__find_eol_start).
 */

#if SVN_UNALIGNED_ACCESS_IS_OK
static svn_boolean_t contains_eol(apr_uintptr_t chunk)
{
  apr_uintptr_t r_test = chunk ^ SVN__R_MASK;
  apr_uintptr_t n_test = chunk ^ SVN__N_MASK;

  r_test |= (r_test & SVN__LOWER_7BITS_SET) + SVN__LOWER_7BITS_SET;
  n_test |= (n_test & SVN__LOWER_7BITS_SET) + SVN__LOWER_7BITS_SET;

  return (r_test & n_test & SVN__BIT_7_SET) != SVN__BIT_7_SET;
}
#endif

/* Find the prefix which is identical between all elements of the FILE array.
 * Return the number of prefix lines in PREFIX_LINES.  REACHED_ONE_EOF will be
 * set to TRUE if one of the FILEs reached its end while scanning prefix,
 * i.e. at least one file consisted entirely of prefix.  Otherwise,
 * REACHED_ONE_EOF is set to FALSE.
 *
 * After this function is finished, the buffers, chunks, curp's and endp's
 * of the FILEs are set to point at the first byte after the prefix. */
static svn_error_t *
find_identical_prefix(svn_boolean_t *reached_one_eof, apr_off_t *prefix_lines,
                      struct file_info file[], apr_size_t file_len,
                      apr_pool_t *pool)
{
  svn_boolean_t had_cr = FALSE;
  svn_boolean_t is_match;
  apr_off_t lines = 0;
  apr_size_t i;

  *reached_one_eof = FALSE;

  for (i = 1, is_match = TRUE; i < file_len; i++)
    is_match = is_match && *file[0].curp == *file[i].curp;
  while (is_match)
    {
#if SVN_UNALIGNED_ACCESS_IS_OK
      apr_ssize_t max_delta, delta;
#endif /* SVN_UNALIGNED_ACCESS_IS_OK */

      /* ### TODO: see if we can take advantage of
         diff options like ignore_eol_style or ignore_space. */
      /* check for eol, and count */
      if (*file[0].curp == '\r')
        {
          lines++;
          had_cr = TRUE;
        }
      else if (*file[0].curp == '\n' && !had_cr)
        {
          lines++;
        }
      else
        {
          had_cr = FALSE;
        }

      INCREMENT_POINTERS(file, file_len, pool);

#if SVN_UNALIGNED_ACCESS_IS_OK

      /* Try to advance as far as possible with machine-word granularity.
       * Determine how far we may advance with chunky ops without reaching
       * endp for any of the files.
       * Signedness is important here if curp gets close to endp.
       */
      max_delta = file[0].endp - file[0].curp - sizeof(apr_uintptr_t);
      for (i = 1; i < file_len; i++)
        {
          delta = file[i].endp - file[i].curp - sizeof(apr_uintptr_t);
          if (delta < max_delta)
            max_delta = delta;
        }

      is_match = TRUE;
      for (delta = 0; delta < max_delta; delta += sizeof(apr_uintptr_t))
        {
          apr_uintptr_t chunk = *(const apr_uintptr_t *)(file[0].curp + delta);
          if (contains_eol(chunk))
            break;

          for (i = 1; i < file_len; i++)
            if (chunk != *(const apr_uintptr_t *)(file[i].curp + delta))
              {
                is_match = FALSE;
                break;
              }

          if (! is_match)
            break;
        }

      if (delta /* > 0*/)
        {
          /* We either found a mismatch or an EOL at or shortly behind curp+delta
           * or we cannot proceed with chunky ops without exceeding endp.
           * In any way, everything up to curp + delta is equal and not an EOL.
           */
          for (i = 0; i < file_len; i++)
            file[i].curp += delta;

          /* Skipped data without EOL markers, so last char was not a CR. */
          had_cr = FALSE;
        }
#endif

      *reached_one_eof = is_one_at_eof(file, file_len);
      if (*reached_one_eof)
        break;
      else
        for (i = 1, is_match = TRUE; i < file_len; i++)
          is_match = is_match && *file[0].curp == *file[i].curp;
    }

  if (had_cr)
    {
      /* Check if we ended in the middle of a \r\n for one file, but \r for
         another. If so, back up one byte, so the next loop will back up
         the entire line. Also decrement lines, since we counted one
         too many for the \r. */
      svn_boolean_t ended_at_nonmatching_newline = FALSE;
      for (i = 0; i < file_len; i++)
        if (file[i].curp < file[i].endp)
          ended_at_nonmatching_newline = ended_at_nonmatching_newline
                                         || *file[i].curp == '\n';
      if (ended_at_nonmatching_newline)
        {
          lines--;
          DECREMENT_POINTERS(file, file_len, pool);
        }
    }

  /* Back up one byte, so we point at the last identical byte */
  DECREMENT_POINTERS(file, file_len, pool);

  /* Back up to the last eol sequence (\n, \r\n or \r) */
  while (!is_one_at_bof(file, file_len) &&
         *file[0].curp != '\n' && *file[0].curp != '\r')
    DECREMENT_POINTERS(file, file_len, pool);

  /* Slide one byte forward, to point past the eol sequence */
  INCREMENT_POINTERS(file, file_len, pool);

  *prefix_lines = lines;

  return SVN_NO_ERROR;
}


/* The number of identical suffix lines to keep with the middle section. These
 * lines are not eliminated as suffix, and can be picked up by the token
 * parsing and lcs steps. This is mainly for backward compatibility with
 * the previous diff (and blame) output (if there are multiple diff solutions,
 * our lcs algorithm prefers taking common lines from the start, rather than
 * from the end. By giving it back some suffix lines, we give it some wiggle
 * room to find the exact same diff as before).
 *
 * The number 50 is more or less arbitrary, based on some real-world tests
 * with big files (and then doubling the required number to be on the safe
 * side). This has a negligible effect on the power of the optimization. */
/* If you change this number, update test_identical_suffix() in diff-diff3-test.c */
#ifndef SUFFIX_LINES_TO_KEEP
#define SUFFIX_LINES_TO_KEEP 50
#endif

/* Find the suffix which is identical between all elements of the FILE array.
 * Return the number of suffix lines in SUFFIX_LINES.
 *
 * Before this function is called the FILEs' pointers and chunks should be
 * positioned right after the identical prefix (which is the case after
 * find_identical_prefix), so we can determine where suffix scanning should
 * ultimately stop. */
static svn_error_t *
find_identical_suffix(apr_off_t *suffix_lines, struct file_info file[],
                      apr_size_t file_len, apr_pool_t *pool)
{
  struct file_info file_for_suffix[4] = { { 0 }  };
  apr_off_t length[4];
  apr_off_t suffix_min_chunk0;
  apr_off_t suffix_min_offset0;
  apr_off_t min_file_size;
  int suffix_lines_to_keep = SUFFIX_LINES_TO_KEEP;
  svn_boolean_t is_match;
  apr_off_t lines = 0;
  svn_boolean_t had_nl;
  apr_size_t i;

  /* Initialize file_for_suffix[].
     Read last chunk, position curp at last byte. */
  for (i = 0; i < file_len; i++)
    {
      file_for_suffix[i].path = file[i].path;
      file_for_suffix[i].file = file[i].file;
      file_for_suffix[i].size = file[i].size;
      file_for_suffix[i].chunk =
        (int) offset_to_chunk(file_for_suffix[i].size); /* last chunk */
      length[i] = offset_in_chunk(file_for_suffix[i].size);
      if (length[i] == 0)
        {
          /* last chunk is an empty chunk -> start at next-to-last chunk */
          file_for_suffix[i].chunk = file_for_suffix[i].chunk - 1;
          length[i] = CHUNK_SIZE;
        }

      if (file_for_suffix[i].chunk == file[i].chunk)
        {
          /* Prefix ended in last chunk, so we can reuse the prefix buffer */
          file_for_suffix[i].buffer = file[i].buffer;
        }
      else
        {
          /* There is at least more than 1 chunk,
             so allocate full chunk size buffer */
          file_for_suffix[i].buffer = apr_palloc(pool, CHUNK_SIZE);
          SVN_ERR(read_chunk(file_for_suffix[i].file,
                             file_for_suffix[i].buffer, length[i],
                             chunk_to_offset(file_for_suffix[i].chunk),
                             pool));
        }
      file_for_suffix[i].endp = file_for_suffix[i].buffer + length[i];
      file_for_suffix[i].curp = file_for_suffix[i].endp - 1;
    }

  /* Get the chunk and pointer offset (for file[0]) at which we should stop
     scanning backward for the identical suffix, i.e. when we reach prefix. */
  suffix_min_chunk0 = file[0].chunk;
  suffix_min_offset0 = file[0].curp - file[0].buffer;

  /* Compensate if other files are smaller than file[0] */
  for (i = 1, min_file_size = file[0].size; i < file_len; i++)
    if (file[i].size < min_file_size)
      min_file_size = file[i].size;
  if (file[0].size > min_file_size)
    {
      suffix_min_chunk0 += (file[0].size - min_file_size) / CHUNK_SIZE;
      suffix_min_offset0 += (file[0].size - min_file_size) % CHUNK_SIZE;
    }

  /* Scan backwards until mismatch or until we reach the prefix. */
  for (i = 1, is_match = TRUE; i < file_len; i++)
    is_match = is_match
               && *file_for_suffix[0].curp == *file_for_suffix[i].curp;
  if (is_match && *file_for_suffix[0].curp != '\r'
               && *file_for_suffix[0].curp != '\n')
    /* Count an extra line for the last line not ending in an eol. */
    lines++;

  had_nl = FALSE;
  while (is_match)
    {
      svn_boolean_t reached_prefix;
#if SVN_UNALIGNED_ACCESS_IS_OK
      /* Initialize the minimum pointer positions. */
      const char *min_curp[4];
      svn_boolean_t can_read_word;
#endif /* SVN_UNALIGNED_ACCESS_IS_OK */

      /* ### TODO: see if we can take advantage of
         diff options like ignore_eol_style or ignore_space. */
      /* check for eol, and count */
      if (*file_for_suffix[0].curp == '\n')
        {
          lines++;
          had_nl = TRUE;
        }
      else if (*file_for_suffix[0].curp == '\r' && !had_nl)
        {
          lines++;
        }
      else
        {
          had_nl = FALSE;
        }

      DECREMENT_POINTERS(file_for_suffix, file_len, pool);

#if SVN_UNALIGNED_ACCESS_IS_OK
      for (i = 0; i < file_len; i++)
        min_curp[i] = file_for_suffix[i].buffer;

      /* If we are in the same chunk that contains the last part of the common
         prefix, use the min_curp[0] pointer to make sure we don't get a
         suffix that overlaps the already determined common prefix. */
      if (file_for_suffix[0].chunk == suffix_min_chunk0)
        min_curp[0] += suffix_min_offset0;

      /* Scan quickly by reading with machine-word granularity. */
      for (i = 0, can_read_word = TRUE; can_read_word && i < file_len; i++)
        can_read_word = ((file_for_suffix[i].curp + 1 - sizeof(apr_uintptr_t))
                         > min_curp[i]);

      while (can_read_word)
        {
          apr_uintptr_t chunk;

          /* For each file curp is positioned at the current byte, but we
             want to examine the current byte and the ones before the current
             location as one machine word. */

          chunk = *(const apr_uintptr_t *)(file_for_suffix[0].curp + 1
                                             - sizeof(apr_uintptr_t));
          if (contains_eol(chunk))
            break;

          for (i = 1, is_match = TRUE; is_match && i < file_len; i++)
            is_match = (chunk
                           == *(const apr_uintptr_t *)
                                    (file_for_suffix[i].curp + 1
                                       - sizeof(apr_uintptr_t)));

          if (! is_match)
            break;

          for (i = 0; i < file_len; i++)
            {
              file_for_suffix[i].curp -= sizeof(apr_uintptr_t);
              can_read_word = can_read_word
                              && (  (file_for_suffix[i].curp + 1
                                       - sizeof(apr_uintptr_t))
                                  > min_curp[i]);
            }

          /* We skipped some bytes, so there are no closing EOLs */
          had_nl = FALSE;
        }

      /* The > min_curp[i] check leaves at least one final byte for checking
         in the non block optimized case below. */
#endif

      reached_prefix = file_for_suffix[0].chunk == suffix_min_chunk0
                       && (file_for_suffix[0].curp - file_for_suffix[0].buffer)
                          == suffix_min_offset0;
      if (reached_prefix || is_one_at_bof(file_for_suffix, file_len))
        break;

      is_match = TRUE;
      for (i = 1; i < file_len; i++)
        is_match = is_match
                   && *file_for_suffix[0].curp == *file_for_suffix[i].curp;
    }

  /* Slide one byte forward, to point at the first byte of identical suffix */
  INCREMENT_POINTERS(file_for_suffix, file_len, pool);

  /* Slide forward until we find an eol sequence to add the rest of the line
     we're in. Then add SUFFIX_LINES_TO_KEEP more lines. Stop if at least
     one file reaches its end. */
  do
    {
      svn_boolean_t had_cr = FALSE;
      while (!is_one_at_eof(file_for_suffix, file_len)
             && *file_for_suffix[0].curp != '\n'
             && *file_for_suffix[0].curp != '\r')
        INCREMENT_POINTERS(file_for_suffix, file_len, pool);

      /* Slide one or two more bytes, to point past the eol. */
      if (!is_one_at_eof(file_for_suffix, file_len)
          && *file_for_suffix[0].curp == '\r')
        {
          lines--;
          had_cr = TRUE;
          INCREMENT_POINTERS(file_for_suffix, file_len, pool);
        }
      if (!is_one_at_eof(file_for_suffix, file_len)
          && *file_for_suffix[0].curp == '\n')
        {
          if (!had_cr)
            lines--;
          INCREMENT_POINTERS(file_for_suffix, file_len, pool);
        }
    }
  while (!is_one_at_eof(file_for_suffix, file_len)
         && suffix_lines_to_keep--);

  if (is_one_at_eof(file_for_suffix, file_len))
    lines = 0;

  /* Save the final suffix information in the original file_info */
  for (i = 0; i < file_len; i++)
    {
      file[i].suffix_start_chunk = file_for_suffix[i].chunk;
      file[i].suffix_offset_in_chunk =
        file_for_suffix[i].curp - file_for_suffix[i].buffer;
    }

  *suffix_lines = lines;

  return SVN_NO_ERROR;
}


/* Let FILE stand for the array of file_info struct elements of BATON->files
 * that are indexed by the elements of the DATASOURCE array.
 * BATON's type is (svn_diff__file_baton_t *).
 *
 * For each file in the FILE array, open the file at FILE.path; initialize
 * FILE.file, FILE.size, FILE.buffer, FILE.curp and FILE.endp; allocate a
 * buffer and read the first chunk.  Then find the prefix and suffix lines
 * which are identical between all the files.  Return the number of identical
 * prefix lines in PREFIX_LINES, and the number of identical suffix lines in
 * SUFFIX_LINES.
 *
 * Finding the identical prefix and suffix allows us to exclude those from the
 * rest of the diff algorithm, which increases performance by reducing the
 * problem space.
 *
 * Implements svn_diff_fns2_t::datasources_open. */
static svn_error_t *
datasources_open(void *baton,
                 apr_off_t *prefix_lines,
                 apr_off_t *suffix_lines,
                 const svn_diff_datasource_e *datasources,
                 apr_size_t datasources_len)
{
  svn_diff__file_baton_t *file_baton = baton;
  struct file_info files[4];
  apr_off_t length[4];
#ifndef SVN_DISABLE_PREFIX_SUFFIX_SCANNING
  svn_boolean_t reached_one_eof;
#endif
  apr_size_t i;

  /* Make sure prefix_lines and suffix_lines are set correctly, even if we
   * exit early because one of the files is empty. */
  *prefix_lines = 0;
  *suffix_lines = 0;

  /* Open datasources and read first chunk */
  for (i = 0; i < datasources_len; i++)
    {
      svn_filesize_t filesize;
      struct file_info *file
          = &file_baton->files[datasource_to_index(datasources[i])];
      SVN_ERR(svn_io_file_open(&file->file, file->path,
                               APR_READ, APR_OS_DEFAULT, file_baton->pool));
      SVN_ERR(svn_io_file_size_get(&filesize, file->file, file_baton->pool));
      file->size = filesize;
      length[i] = filesize > CHUNK_SIZE ? CHUNK_SIZE : filesize;
      file->buffer = apr_palloc(file_baton->pool, (apr_size_t) length[i]);
      SVN_ERR(read_chunk(file->file, file->buffer,
                         length[i], 0, file_baton->pool));
      file->endp = file->buffer + length[i];
      file->curp = file->buffer;
      /* Set suffix_start_chunk to a guard value, so if suffix scanning is
       * skipped because one of the files is empty, or because of
       * reached_one_eof, we can still easily check for the suffix during
       * token reading (datasource_get_next_token). */
      file->suffix_start_chunk = -1;

      files[i] = *file;
    }

  for (i = 0; i < datasources_len; i++)
    if (length[i] == 0)
      /* There will not be any identical prefix/suffix, so we're done. */
      return SVN_NO_ERROR;

#ifndef SVN_DISABLE_PREFIX_SUFFIX_SCANNING

  SVN_ERR(find_identical_prefix(&reached_one_eof, prefix_lines,
                                files, datasources_len, file_baton->pool));

  if (!reached_one_eof)
    /* No file consisted totally of identical prefix,
     * so there may be some identical suffix.  */
    SVN_ERR(find_identical_suffix(suffix_lines, files, datasources_len,
                                  file_baton->pool));

#endif

  /* Copy local results back to baton. */
  for (i = 0; i < datasources_len; i++)
    file_baton->files[datasource_to_index(datasources[i])] = files[i];

  return SVN_NO_ERROR;
}


/* Implements svn_diff_fns2_t::datasource_close */
static svn_error_t *
datasource_close(void *baton, svn_diff_datasource_e datasource)
{
  /* Do nothing.  The compare_token function needs previous datasources
   * to stay available until all datasources are processed.
   */

  return SVN_NO_ERROR;
}

/* Implements svn_diff_fns2_t::datasource_get_next_token */
static svn_error_t *
datasource_get_next_token(apr_uint32_t *hash, void **token, void *baton,
                          svn_diff_datasource_e datasource)
{
  svn_diff__file_baton_t *file_baton = baton;
  svn_diff__file_token_t *file_token;
  struct file_info *file = &file_baton->files[datasource_to_index(datasource)];
  char *endp;
  char *curp;
  char *eol;
  apr_off_t last_chunk;
  apr_off_t length;
  apr_uint32_t h = 0;
  /* Did the last chunk end in a CR character? */
  svn_boolean_t had_cr = FALSE;

  *token = NULL;

  curp = file->curp;
  endp = file->endp;

  last_chunk = offset_to_chunk(file->size);

  /* Are we already at the end of a chunk? */
  if (curp == endp)
    {
      /* Are we at EOF */
      if (last_chunk == file->chunk)
        return SVN_NO_ERROR; /* EOF */

      /* Or right before an identical suffix in the next chunk? */
      if (file->chunk + 1 == file->suffix_start_chunk
          && file->suffix_offset_in_chunk == 0)
        return SVN_NO_ERROR;
    }

  /* Stop when we encounter the identical suffix. If suffix scanning was not
   * performed, suffix_start_chunk will be -1, so this condition will never
   * be true. */
  if (file->chunk == file->suffix_start_chunk
      && (curp - file->buffer) == file->suffix_offset_in_chunk)
    return SVN_NO_ERROR;

  /* Allocate a new token, or fetch one from the "reusable tokens" list. */
  file_token = file_baton->tokens;
  if (file_token)
    {
      file_baton->tokens = file_token->next;
    }
  else
    {
      file_token = apr_palloc(file_baton->pool, sizeof(*file_token));
    }

  file_token->datasource = datasource;
  file_token->offset = chunk_to_offset(file->chunk)
                       + (curp - file->buffer);
  file_token->norm_offset = file_token->offset;
  file_token->raw_length = 0;
  file_token->length = 0;

  while (1)
    {
      eol = svn_eol__find_eol_start(curp, endp - curp);
      if (eol)
        {
          had_cr = (*eol == '\r');
          eol++;
          /* If we have the whole eol sequence in the chunk... */
          if (!(had_cr && eol == endp))
            {
              /* Also skip past the '\n' in an '\r\n' sequence. */
              if (had_cr && *eol == '\n')
                eol++;
              break;
            }
        }

      if (file->chunk == last_chunk)
        {
          eol = endp;
          break;
        }

      length = endp - curp;
      file_token->raw_length += length;
      {
        char *c = curp;

        svn_diff__normalize_buffer(&c, &length,
                                   &file->normalize_state,
                                   curp, file_baton->options);
        if (file_token->length == 0)
          {
            /* When we are reading the first part of the token, move the
               normalized offset past leading ignored characters, if any. */
            file_token->norm_offset += (c - curp);
          }
        file_token->length += length;
        h = svn__adler32(h, c, length);
      }

      curp = endp = file->buffer;
      file->chunk++;
      length = file->chunk == last_chunk ?
        offset_in_chunk(file->size) : CHUNK_SIZE;
      endp += length;
      file->endp = endp;

      /* Issue #4283: Normally we should have checked for reaching the skipped
         suffix here, but because we assume that a suffix always starts on a
         line and token boundary we rely on catching the suffix earlier in this
         function.

         When changing things here, make sure the whitespace settings are
         applied, or we might not reach the exact suffix boundary as token
         boundary. */
      SVN_ERR(read_chunk(file->file,
                         curp, length,
                         chunk_to_offset(file->chunk),
                         file_baton->pool));

      /* If the last chunk ended in a CR, we're done. */
      if (had_cr)
        {
          eol = curp;
          if (*curp == '\n')
            ++eol;
          break;
        }
    }

  length = eol - curp;
  file_token->raw_length += length;
  file->curp = eol;

  /* If the file length is exactly a multiple of CHUNK_SIZE, we will end up
   * with a spurious empty token.  Avoid returning it.
   * Note that we use the unnormalized length; we don't want a line containing
   * only spaces (and no trailing newline) to appear like a non-existent
   * line. */
  if (file_token->raw_length > 0)
    {
      char *c = curp;
      svn_diff__normalize_buffer(&c, &length,
                                 &file->normalize_state,
                                 curp, file_baton->options);
      if (file_token->length == 0)
        {
          /* When we are reading the first part of the token, move the
             normalized offset past leading ignored characters, if any. */
          file_token->norm_offset += (c - curp);
        }

      file_token->length += length;

      *hash = svn__adler32(h, c, length);
      *token = file_token;
    }

  return SVN_NO_ERROR;
}

#define COMPARE_CHUNK_SIZE 4096

/* Implements svn_diff_fns2_t::token_compare */
static svn_error_t *
token_compare(void *baton, void *token1, void *token2, int *compare)
{
  svn_diff__file_baton_t *file_baton = baton;
  svn_diff__file_token_t *file_token[2];
  char buffer[2][COMPARE_CHUNK_SIZE];
  char *bufp[2];
  apr_off_t offset[2];
  struct file_info *file[2];
  apr_off_t length[2];
  apr_off_t total_length;
  /* How much is left to read of each token from the file. */
  apr_off_t raw_length[2];
  int i;
  svn_diff__normalize_state_t state[2];

  file_token[0] = token1;
  file_token[1] = token2;
  if (file_token[0]->length < file_token[1]->length)
    {
      *compare = -1;
      return SVN_NO_ERROR;
    }

  if (file_token[0]->length > file_token[1]->length)
    {
      *compare = 1;
      return SVN_NO_ERROR;
    }

  total_length = file_token[0]->length;
  if (total_length == 0)
    {
      *compare = 0;
      return SVN_NO_ERROR;
    }

  for (i = 0; i < 2; ++i)
    {
      int idx = datasource_to_index(file_token[i]->datasource);

      file[i] = &file_baton->files[idx];
      offset[i] = file_token[i]->norm_offset;
      state[i] = svn_diff__normalize_state_normal;

      if (offset_to_chunk(offset[i]) == file[i]->chunk)
        {
          /* If the start of the token is in memory, the entire token is
           * in memory.
           */
          bufp[i] = file[i]->buffer;
          bufp[i] += offset_in_chunk(offset[i]);

          length[i] = total_length;
          raw_length[i] = 0;
        }
      else
        {
          apr_off_t skipped;

          length[i] = 0;

          /* When we skipped the first part of the token via the whitespace
             normalization we must reduce the raw length of the token */
          skipped = (file_token[i]->norm_offset - file_token[i]->offset);

          raw_length[i] = file_token[i]->raw_length - skipped;
        }
    }

  do
    {
      apr_off_t len;
      for (i = 0; i < 2; i++)
        {
          if (length[i] == 0)
            {
              /* Error if raw_length is 0, that's an unexpected change
               * of the file that can happen when ingoring whitespace
               * and that can lead to an infinite loop. */
              if (raw_length[i] == 0)
                return svn_error_createf(SVN_ERR_DIFF_DATASOURCE_MODIFIED,
                                         NULL,
                                         _("The file '%s' changed unexpectedly"
                                           " during diff"),
                                         file[i]->path);

              /* Read a chunk from disk into a buffer */
              bufp[i] = buffer[i];
              length[i] = raw_length[i] > COMPARE_CHUNK_SIZE ?
                COMPARE_CHUNK_SIZE : raw_length[i];

              SVN_ERR(read_chunk(file[i]->file,
                                 bufp[i], length[i], offset[i],
                                 file_baton->pool));
              offset[i] += length[i];
              raw_length[i] -= length[i];
              /* bufp[i] gets reset to buffer[i] before reading each chunk,
                 so, overwriting it isn't a problem */
              svn_diff__normalize_buffer(&bufp[i], &length[i], &state[i],
                                         bufp[i], file_baton->options);

              /* assert(length[i] == file_token[i]->length); */
            }
        }

      len = length[0] > length[1] ? length[1] : length[0];

      /* Compare two chunks (that could be entire tokens if they both reside
       * in memory).
       */
      *compare = memcmp(bufp[0], bufp[1], (size_t) len);
      if (*compare != 0)
        return SVN_NO_ERROR;

      total_length -= len;
      length[0] -= len;
      length[1] -= len;
      bufp[0] += len;
      bufp[1] += len;
    }
  while(total_length > 0);

  *compare = 0;
  return SVN_NO_ERROR;
}


/* Implements svn_diff_fns2_t::token_discard */
static void
token_discard(void *baton, void *token)
{
  svn_diff__file_baton_t *file_baton = baton;
  svn_diff__file_token_t *file_token = token;

  /* Prepend FILE_TOKEN to FILE_BATON->TOKENS, for reuse. */
  file_token->next = file_baton->tokens;
  file_baton->tokens = file_token;
}


/* Implements svn_diff_fns2_t::token_discard_all */
static void
token_discard_all(void *baton)
{
  svn_diff__file_baton_t *file_baton = baton;

  /* Discard all memory in use by the tokens, and close all open files. */
  svn_pool_clear(file_baton->pool);
}


static const svn_diff_fns2_t svn_diff__file_vtable =
{
  datasources_open,
  datasource_close,
  datasource_get_next_token,
  token_compare,
  token_discard,
  token_discard_all
};

/* Id for the --ignore-eol-style option, which doesn't have a short name. */
#define SVN_DIFF__OPT_IGNORE_EOL_STYLE 256

/* Options supported by svn_diff_file_options_parse(). */
static const apr_getopt_option_t diff_options[] =
{
  { "ignore-space-change", 'b', 0, NULL },
  { "ignore-all-space", 'w', 0, NULL },
  { "ignore-eol-style", SVN_DIFF__OPT_IGNORE_EOL_STYLE, 0, NULL },
  { "show-c-function", 'p', 0, NULL },
  /* ### For compatibility; we don't support the argument to -u, because
   * ### we don't have optional argument support. */
  { "unified", 'u', 0, NULL },
  { "context", 'U', 1, NULL },
  { NULL, 0, 0, NULL }
};

svn_diff_file_options_t *
svn_diff_file_options_create(apr_pool_t *pool)
{
  svn_diff_file_options_t * opts = apr_pcalloc(pool, sizeof(*opts));

  opts->context_size = SVN_DIFF__UNIFIED_CONTEXT_SIZE;

  return opts;
}

/* A baton for use with opt_parsing_error_func(). */
struct opt_parsing_error_baton_t
{
  svn_error_t *err;
  apr_pool_t *pool;
};

/* Store an error message from apr_getopt_long().  Set BATON->err to a new
 * error with a message generated from FMT and the remaining arguments.
 * Implements apr_getopt_err_fn_t. */
static void
opt_parsing_error_func(void *baton,
                       const char *fmt, ...)
{
  struct opt_parsing_error_baton_t *b = baton;
  const char *message;
  va_list ap;

  va_start(ap, fmt);
  message = apr_pvsprintf(b->pool, fmt, ap);
  va_end(ap);

  /* Skip leading ": " (if present, which it always is in known cases). */
  if (strncmp(message, ": ", 2) == 0)
    message += 2;

  b->err = svn_error_create(SVN_ERR_INVALID_DIFF_OPTION, NULL, message);
}

svn_error_t *
svn_diff_file_options_parse(svn_diff_file_options_t *options,
                            const apr_array_header_t *args,
                            apr_pool_t *pool)
{
  apr_getopt_t *os;
  struct opt_parsing_error_baton_t opt_parsing_error_baton;
  apr_array_header_t *argv;

  opt_parsing_error_baton.err = NULL;
  opt_parsing_error_baton.pool = pool;

  /* Make room for each option (starting at index 1) plus trailing NULL. */
  argv = apr_array_make(pool, args->nelts + 2, sizeof(char*));
  APR_ARRAY_PUSH(argv, const char *) = "";
  apr_array_cat(argv, args);
  APR_ARRAY_PUSH(argv, const char *) = NULL;

  apr_getopt_init(&os, pool, 
                  argv->nelts - 1 /* Exclude trailing NULL */,
                  (const char *const *) argv->elts);

  /* Capture any error message from apr_getopt_long().  This will typically
   * say which option is wrong, which we would not otherwise know. */
  os->errfn = opt_parsing_error_func;
  os->errarg = &opt_parsing_error_baton;

  while (1)
    {
      const char *opt_arg;
      int opt_id;
      apr_status_t err = apr_getopt_long(os, diff_options, &opt_id, &opt_arg);

      if (APR_STATUS_IS_EOF(err))
        break;
      if (err)
        /* Wrap apr_getopt_long()'s error message.  Its doc string implies
         * it always will produce one, but never mind if it doesn't.  Avoid
         * using the message associated with the return code ERR, because
         * it refers to the "command line" which may be misleading here. */
        return svn_error_create(SVN_ERR_INVALID_DIFF_OPTION,
                                opt_parsing_error_baton.err,
                                _("Error in options to internal diff"));

      switch (opt_id)
        {
        case 'b':
          /* -w takes precedence over -b. */
          if (! options->ignore_space)
            options->ignore_space = svn_diff_file_ignore_space_change;
          break;
        case 'w':
          options->ignore_space = svn_diff_file_ignore_space_all;
          break;
        case SVN_DIFF__OPT_IGNORE_EOL_STYLE:
          options->ignore_eol_style = TRUE;
          break;
        case 'p':
          options->show_c_function = TRUE;
          break;
        case 'U':
          SVN_ERR(svn_cstring_atoi(&options->context_size, opt_arg));
          break;
        default:
          break;
        }
    }

  /* Check for spurious arguments. */
  if (os->ind < os->argc)
    return svn_error_createf(SVN_ERR_INVALID_DIFF_OPTION, NULL,
                             _("Invalid argument '%s' in diff options"),
                             os->argv[os->ind]);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff_file_diff_2(svn_diff_t **diff,
                     const char *original,
                     const char *modified,
                     const svn_diff_file_options_t *options,
                     apr_pool_t *pool)
{
  svn_diff__file_baton_t baton = { 0 };

  baton.options = options;
  baton.files[0].path = original;
  baton.files[1].path = modified;
  baton.pool = svn_pool_create(pool);

  SVN_ERR(svn_diff_diff_2(diff, &baton, &svn_diff__file_vtable, pool));

  svn_pool_destroy(baton.pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff_file_diff3_2(svn_diff_t **diff,
                      const char *original,
                      const char *modified,
                      const char *latest,
                      const svn_diff_file_options_t *options,
                      apr_pool_t *pool)
{
  svn_diff__file_baton_t baton = { 0 };

  baton.options = options;
  baton.files[0].path = original;
  baton.files[1].path = modified;
  baton.files[2].path = latest;
  baton.pool = svn_pool_create(pool);

  SVN_ERR(svn_diff_diff3_2(diff, &baton, &svn_diff__file_vtable, pool));

  svn_pool_destroy(baton.pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff_file_diff4_2(svn_diff_t **diff,
                      const char *original,
                      const char *modified,
                      const char *latest,
                      const char *ancestor,
                      const svn_diff_file_options_t *options,
                      apr_pool_t *pool)
{
  svn_diff__file_baton_t baton = { 0 };

  baton.options = options;
  baton.files[0].path = original;
  baton.files[1].path = modified;
  baton.files[2].path = latest;
  baton.files[3].path = ancestor;
  baton.pool = svn_pool_create(pool);

  SVN_ERR(svn_diff_diff4_2(diff, &baton, &svn_diff__file_vtable, pool));

  svn_pool_destroy(baton.pool);
  return SVN_NO_ERROR;
}


/** Display unified context diffs **/

/* Maximum length of the extra context to show when show_c_function is set.
 * GNU diff uses 40, let's be brave and use 50 instead. */
#define SVN_DIFF__EXTRA_CONTEXT_LENGTH 50
typedef struct svn_diff__file_output_baton_t
{
  svn_stream_t *output_stream;
  const char *header_encoding;

  /* Cached markers, in header_encoding. */
  const char *context_str;
  const char *delete_str;
  const char *insert_str;

  const char *path[2];
  apr_file_t *file[2];

  apr_off_t   current_line[2];

  char        buffer[2][4096];
  apr_size_t  length[2];
  char       *curp[2];

  apr_off_t   hunk_start[2];
  apr_off_t   hunk_length[2];
  svn_stringbuf_t *hunk;

  /* Should we emit C functions in the unified diff header */
  svn_boolean_t show_c_function;
  /* Extra strings to skip over if we match. */
  apr_array_header_t *extra_skip_match;
  /* "Context" to append to the @@ line when the show_c_function option
   * is set. */
  svn_stringbuf_t *extra_context;
  /* Extra context for the current hunk. */
  char hunk_extra_context[SVN_DIFF__EXTRA_CONTEXT_LENGTH + 1];

  int context_size;

  /* Cancel handler */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  apr_pool_t *pool;
} svn_diff__file_output_baton_t;

typedef enum svn_diff__file_output_unified_type_e
{
  svn_diff__file_output_unified_skip,
  svn_diff__file_output_unified_context,
  svn_diff__file_output_unified_delete,
  svn_diff__file_output_unified_insert
} svn_diff__file_output_unified_type_e;


static svn_error_t *
output_unified_line(svn_diff__file_output_baton_t *baton,
                    svn_diff__file_output_unified_type_e type, int idx)
{
  char *curp;
  char *eol;
  apr_size_t length;
  svn_error_t *err;
  svn_boolean_t bytes_processed = FALSE;
  svn_boolean_t had_cr = FALSE;
  /* Are we collecting extra context? */
  svn_boolean_t collect_extra = FALSE;

  length = baton->length[idx];
  curp = baton->curp[idx];

  /* Lazily update the current line even if we're at EOF.
   * This way we fake output of context at EOF
   */
  baton->current_line[idx]++;

  if (length == 0 && apr_file_eof(baton->file[idx]))
    {
      return SVN_NO_ERROR;
    }

  do
    {
      if (length > 0)
        {
          if (!bytes_processed)
            {
              switch (type)
                {
                case svn_diff__file_output_unified_context:
                  svn_stringbuf_appendcstr(baton->hunk, baton->context_str);
                  baton->hunk_length[0]++;
                  baton->hunk_length[1]++;
                  break;
                case svn_diff__file_output_unified_delete:
                  svn_stringbuf_appendcstr(baton->hunk, baton->delete_str);
                  baton->hunk_length[0]++;
                  break;
                case svn_diff__file_output_unified_insert:
                  svn_stringbuf_appendcstr(baton->hunk, baton->insert_str);
                  baton->hunk_length[1]++;
                  break;
                default:
                  break;
                }

              if (baton->show_c_function
                  && (type == svn_diff__file_output_unified_skip
                      || type == svn_diff__file_output_unified_context)
                  && (svn_ctype_isalpha(*curp) || *curp == '$' || *curp == '_')
                  && !svn_cstring_match_glob_list(curp,
                                                  baton->extra_skip_match))
                {
                  svn_stringbuf_setempty(baton->extra_context);
                  collect_extra = TRUE;
                }
            }

          eol = svn_eol__find_eol_start(curp, length);

          if (eol != NULL)
            {
              apr_size_t len;

              had_cr = (*eol == '\r');
              eol++;
              len = (apr_size_t)(eol - curp);

              if (! had_cr || len < length)
                {
                  if (had_cr && *eol == '\n')
                    {
                      ++eol;
                      ++len;
                    }

                  length -= len;

                  if (type != svn_diff__file_output_unified_skip)
                    {
                      svn_stringbuf_appendbytes(baton->hunk, curp, len);
                    }
                  if (collect_extra)
                    {
                      svn_stringbuf_appendbytes(baton->extra_context,
                                                curp, len);
                    }

                  baton->curp[idx] = eol;
                  baton->length[idx] = length;

                  err = SVN_NO_ERROR;

                  break;
                }
            }

          if (type != svn_diff__file_output_unified_skip)
            {
              svn_stringbuf_appendbytes(baton->hunk, curp, length);
            }

          if (collect_extra)
            {
              svn_stringbuf_appendbytes(baton->extra_context, curp, length);
            }

          bytes_processed = TRUE;
        }

      curp = baton->buffer[idx];
      length = sizeof(baton->buffer[idx]);

      err = svn_io_file_read(baton->file[idx], curp, &length, baton->pool);

      /* If the last chunk ended with a CR, we look for an LF at the start
         of this chunk. */
      if (had_cr)
        {
          if (! err && length > 0 && *curp == '\n')
            {
              if (type != svn_diff__file_output_unified_skip)
                {
                  svn_stringbuf_appendbyte(baton->hunk, *curp);
                }
              /* We don't append the LF to extra_context, since it would
               * just be stripped anyway. */
              ++curp;
              --length;
            }

          baton->curp[idx] = curp;
          baton->length[idx] = length;

          break;
        }
    }
  while (! err);

  if (err && ! APR_STATUS_IS_EOF(err->apr_err))
    return err;

  if (err && APR_STATUS_IS_EOF(err->apr_err))
    {
      svn_error_clear(err);
      /* Special case if we reach the end of file AND the last line is in the
         changed range AND the file doesn't end with a newline */
      if (bytes_processed && (type != svn_diff__file_output_unified_skip)
          && ! had_cr)
        {
          SVN_ERR(svn_diff__unified_append_no_newline_msg(
                    baton->hunk, baton->header_encoding, baton->pool));
        }

      baton->length[idx] = 0;
    }

  return SVN_NO_ERROR;
}

static APR_INLINE svn_error_t *
output_unified_diff_range(svn_diff__file_output_baton_t *output_baton,
                          int source,
                          svn_diff__file_output_unified_type_e type,
                          apr_off_t until,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton)
{
  while (output_baton->current_line[source] < until)
    {
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      SVN_ERR(output_unified_line(output_baton, type, source));
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
output_unified_flush_hunk(svn_diff__file_output_baton_t *baton)
{
  apr_off_t target_line;
  apr_size_t hunk_len;
  apr_off_t old_start;
  apr_off_t new_start;

  if (svn_stringbuf_isempty(baton->hunk))
    {
      /* Nothing to flush */
      return SVN_NO_ERROR;
    }

  target_line = baton->hunk_start[0] + baton->hunk_length[0]
                + baton->context_size;

  /* Add trailing context to the hunk */
  SVN_ERR(output_unified_diff_range(baton, 0 /* original */,
                                    svn_diff__file_output_unified_context,
                                    target_line,
                                    baton->cancel_func, baton->cancel_baton));

  old_start = baton->hunk_start[0];
  new_start = baton->hunk_start[1];

  /* If the file is non-empty, convert the line indexes from
     zero based to one based */
  if (baton->hunk_length[0])
    old_start++;
  if (baton->hunk_length[1])
    new_start++;

  /* Write the hunk header */
  SVN_ERR(svn_diff__unified_write_hunk_header(
            baton->output_stream, baton->header_encoding, "@@",
            old_start, baton->hunk_length[0],
            new_start, baton->hunk_length[1],
            baton->hunk_extra_context,
            baton->pool));

  /* Output the hunk content */
  hunk_len = baton->hunk->len;
  SVN_ERR(svn_stream_write(baton->output_stream, baton->hunk->data,
                           &hunk_len));

  /* Prepare for the next hunk */
  baton->hunk_length[0] = 0;
  baton->hunk_length[1] = 0;
  baton->hunk_start[0] = 0;
  baton->hunk_start[1] = 0;
  svn_stringbuf_setempty(baton->hunk);

  return SVN_NO_ERROR;
}

static svn_error_t *
output_unified_diff_modified(void *baton,
  apr_off_t original_start, apr_off_t original_length,
  apr_off_t modified_start, apr_off_t modified_length,
  apr_off_t latest_start, apr_off_t latest_length)
{
  svn_diff__file_output_baton_t *output_baton = baton;
  apr_off_t context_prefix_length;
  apr_off_t prev_context_end;
  svn_boolean_t init_hunk = FALSE;

  if (original_start > output_baton->context_size)
    context_prefix_length = output_baton->context_size;
  else
    context_prefix_length = original_start;

  /* Calculate where the previous hunk will end if we would write it now
     (including the necessary context at the end) */
  if (output_baton->hunk_length[0] > 0 || output_baton->hunk_length[1] > 0)
    {
      prev_context_end = output_baton->hunk_start[0]
                         + output_baton->hunk_length[0]
                         + output_baton->context_size;
    }
  else
    {
      prev_context_end = -1;

      if (output_baton->hunk_start[0] == 0
          && (original_length > 0 || modified_length > 0))
        init_hunk = TRUE;
    }

  /* If the changed range is far enough from the previous range, flush the current
     hunk. */
  {
    apr_off_t new_hunk_start = (original_start - context_prefix_length);

    if (output_baton->current_line[0] < new_hunk_start
          && prev_context_end <= new_hunk_start)
      {
        SVN_ERR(output_unified_flush_hunk(output_baton));
        init_hunk = TRUE;
      }
    else if (output_baton->hunk_length[0] > 0
             || output_baton->hunk_length[1] > 0)
      {
        /* We extend the current hunk */


        /* Original: Output the context preceding the changed range */
        SVN_ERR(output_unified_diff_range(output_baton, 0 /* original */,
                                          svn_diff__file_output_unified_context,
                                          original_start,
                                          output_baton->cancel_func,
                                          output_baton->cancel_baton));
      }
  }

  /* Original: Skip lines until we are at the beginning of the context we want
     to display */
  SVN_ERR(output_unified_diff_range(output_baton, 0 /* original */,
                                    svn_diff__file_output_unified_skip,
                                    original_start - context_prefix_length,
                                    output_baton->cancel_func,
                                    output_baton->cancel_baton));

  /* Note that the above skip stores data for the show_c_function support below */

  if (init_hunk)
    {
      SVN_ERR_ASSERT(output_baton->hunk_length[0] == 0
                     && output_baton->hunk_length[1] == 0);

      output_baton->hunk_start[0] = original_start - context_prefix_length;
      output_baton->hunk_start[1] = modified_start - context_prefix_length;
    }

  if (init_hunk && output_baton->show_c_function)
    {
      apr_size_t p;
      const char *invalid_character;

      /* Save the extra context for later use.
       * Note that the last byte of the hunk_extra_context array is never
       * touched after it is zero-initialized, so the array is always
       * 0-terminated. */
      strncpy(output_baton->hunk_extra_context,
              output_baton->extra_context->data,
              SVN_DIFF__EXTRA_CONTEXT_LENGTH);
      /* Trim whitespace at the end, most notably to get rid of any
       * newline characters. */
      p = strlen(output_baton->hunk_extra_context);
      while (p > 0
             && svn_ctype_isspace(output_baton->hunk_extra_context[p - 1]))
        {
          output_baton->hunk_extra_context[--p] = '\0';
        }
      invalid_character =
        svn_utf__last_valid(output_baton->hunk_extra_context,
                            SVN_DIFF__EXTRA_CONTEXT_LENGTH);
      for (p = invalid_character - output_baton->hunk_extra_context;
           p < SVN_DIFF__EXTRA_CONTEXT_LENGTH; p++)
        {
          output_baton->hunk_extra_context[p] = '\0';
        }
    }

  /* Modified: Skip lines until we are at the start of the changed range */
  SVN_ERR(output_unified_diff_range(output_baton, 1 /* modified */,
                                    svn_diff__file_output_unified_skip,
                                    modified_start,
                                    output_baton->cancel_func,
                                    output_baton->cancel_baton));

  /* Original: Output the context preceding the changed range */
  SVN_ERR(output_unified_diff_range(output_baton, 0 /* original */,
                                    svn_diff__file_output_unified_context,
                                    original_start,
                                    output_baton->cancel_func,
                                    output_baton->cancel_baton));

  /* Both: Output the changed range */
  SVN_ERR(output_unified_diff_range(output_baton, 0 /* original */,
                                    svn_diff__file_output_unified_delete,
                                    original_start + original_length,
                                    output_baton->cancel_func,
                                    output_baton->cancel_baton));
  SVN_ERR(output_unified_diff_range(output_baton, 1 /* modified */,
                                    svn_diff__file_output_unified_insert,
                                    modified_start + modified_length,
                                    output_baton->cancel_func,
                                    output_baton->cancel_baton));

  return SVN_NO_ERROR;
}

/* Set *HEADER to a new string consisting of PATH, a tab, and PATH's mtime. */
static svn_error_t *
output_unified_default_hdr(const char **header, const char *path,
                           apr_pool_t *pool)
{
  apr_finfo_t file_info;
  apr_time_exp_t exploded_time;
  char time_buffer[64];
  apr_size_t time_len;
  const char *utf8_timestr;

  SVN_ERR(svn_io_stat(&file_info, path, APR_FINFO_MTIME, pool));
  apr_time_exp_lt(&exploded_time, file_info.mtime);

  apr_strftime(time_buffer, &time_len, sizeof(time_buffer) - 1,
  /* Order of date components can be different in different languages */
               _("%a %b %e %H:%M:%S %Y"), &exploded_time);

  SVN_ERR(svn_utf_cstring_to_utf8(&utf8_timestr, time_buffer, pool));

  *header = apr_psprintf(pool, "%s\t%s", path, utf8_timestr);

  return SVN_NO_ERROR;
}

static const svn_diff_output_fns_t svn_diff__file_output_unified_vtable =
{
  NULL, /* output_common */
  output_unified_diff_modified,
  NULL, /* output_diff_latest */
  NULL, /* output_diff_common */
  NULL  /* output_conflict */
};

svn_error_t *
svn_diff_file_output_unified4(svn_stream_t *output_stream,
                              svn_diff_t *diff,
                              const char *original_path,
                              const char *modified_path,
                              const char *original_header,
                              const char *modified_header,
                              const char *header_encoding,
                              const char *relative_to_dir,
                              svn_boolean_t show_c_function,
                              int context_size,
                              svn_cancel_func_t cancel_func,
                              void *cancel_baton,
                              apr_pool_t *pool)
{
  if (svn_diff_contains_diffs(diff))
    {
      svn_diff__file_output_baton_t baton;
      int i;

      memset(&baton, 0, sizeof(baton));
      baton.output_stream = output_stream;
      baton.cancel_func = cancel_func;
      baton.cancel_baton = cancel_baton;
      baton.pool = pool;
      baton.header_encoding = header_encoding;
      baton.path[0] = original_path;
      baton.path[1] = modified_path;
      baton.hunk = svn_stringbuf_create_empty(pool);
      baton.show_c_function = show_c_function;
      baton.extra_context = svn_stringbuf_create_empty(pool);
      baton.context_size = (context_size >= 0) ? context_size
                                              : SVN_DIFF__UNIFIED_CONTEXT_SIZE;

      if (show_c_function)
        {
          baton.extra_skip_match = apr_array_make(pool, 3, sizeof(char **));

          APR_ARRAY_PUSH(baton.extra_skip_match, const char *) = "public:*";
          APR_ARRAY_PUSH(baton.extra_skip_match, const char *) = "private:*";
          APR_ARRAY_PUSH(baton.extra_skip_match, const char *) = "protected:*";
        }

      SVN_ERR(svn_utf_cstring_from_utf8_ex2(&baton.context_str, " ",
                                            header_encoding, pool));
      SVN_ERR(svn_utf_cstring_from_utf8_ex2(&baton.delete_str, "-",
                                            header_encoding, pool));
      SVN_ERR(svn_utf_cstring_from_utf8_ex2(&baton.insert_str, "+",
                                            header_encoding, pool));

      if (relative_to_dir)
        {
          /* Possibly adjust the "original" and "modified" paths shown in
             the output (see issue #2723). */
          const char *child_path;

          if (! original_header)
            {
              child_path = svn_dirent_is_child(relative_to_dir,
                                               original_path, pool);
              if (child_path)
                original_path = child_path;
              else
                return svn_error_createf(
                                   SVN_ERR_BAD_RELATIVE_PATH, NULL,
                                   _("Path '%s' must be inside "
                                     "the directory '%s'"),
                                   svn_dirent_local_style(original_path, pool),
                                   svn_dirent_local_style(relative_to_dir,
                                                          pool));
            }

          if (! modified_header)
            {
              child_path = svn_dirent_is_child(relative_to_dir,
                                               modified_path, pool);
              if (child_path)
                modified_path = child_path;
              else
                return svn_error_createf(
                                   SVN_ERR_BAD_RELATIVE_PATH, NULL,
                                   _("Path '%s' must be inside "
                                     "the directory '%s'"),
                                   svn_dirent_local_style(modified_path, pool),
                                   svn_dirent_local_style(relative_to_dir,
                                                          pool));
            }
        }

      for (i = 0; i < 2; i++)
        {
          SVN_ERR(svn_io_file_open(&baton.file[i], baton.path[i],
                                   APR_READ, APR_OS_DEFAULT, pool));
        }

      if (original_header == NULL)
        {
          SVN_ERR(output_unified_default_hdr(&original_header, original_path,
                                             pool));
        }

      if (modified_header == NULL)
        {
          SVN_ERR(output_unified_default_hdr(&modified_header, modified_path,
                                             pool));
        }

      SVN_ERR(svn_diff__unidiff_write_header(output_stream, header_encoding,
                                             original_header, modified_header,
                                             pool));

      SVN_ERR(svn_diff_output2(diff, &baton,
                               &svn_diff__file_output_unified_vtable,
                               cancel_func, cancel_baton));
      SVN_ERR(output_unified_flush_hunk(&baton));

      for (i = 0; i < 2; i++)
        {
          SVN_ERR(svn_io_file_close(baton.file[i], pool));
        }
    }

  return SVN_NO_ERROR;
}


/** Display diff3 **/

/* A stream to remember *leading* context.  Note that this stream does
   *not* copy the data that it is remembering; it just saves
   *pointers! */
typedef struct context_saver_t {
  svn_stream_t *stream;
  int context_size;
  const char **data; /* const char *data[context_size] */
  apr_size_t *len;   /* apr_size_t len[context_size] */
  apr_size_t next_slot;
  apr_ssize_t total_writes;
} context_saver_t;


static svn_error_t *
context_saver_stream_write(void *baton,
                           const char *data,
                           apr_size_t *len)
{
  context_saver_t *cs = baton;

  if (cs->context_size > 0)
    {
      cs->data[cs->next_slot] = data;
      cs->len[cs->next_slot] = *len;
      cs->next_slot = (cs->next_slot + 1) % cs->context_size;
      cs->total_writes++;
    }
  return SVN_NO_ERROR;
}

typedef struct svn_diff3__file_output_baton_t
{
  svn_stream_t *output_stream;

  const char *path[3];

  apr_off_t   current_line[3];

  char       *buffer[3];
  char       *endp[3];
  char       *curp[3];

  /* The following four members are in the encoding used for the output. */
  const char *conflict_modified;
  const char *conflict_original;
  const char *conflict_separator;
  const char *conflict_latest;

  const char *marker_eol;

  svn_diff_conflict_display_style_t conflict_style;
  int context_size;

  /* cancel support */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  /* The rest of the fields are for
     svn_diff_conflict_display_only_conflicts only.  Note that for
     these batons, OUTPUT_STREAM is either CONTEXT_SAVER->STREAM or
     (soon after a conflict) a "trailing context stream", never the
     actual output stream.*/
  /* The actual output stream. */
  svn_stream_t *real_output_stream;
  context_saver_t *context_saver;
  /* Used to allocate context_saver and trailing context streams, and
     for some printfs. */
  apr_pool_t *pool;
} svn_diff3__file_output_baton_t;

static svn_error_t *
flush_context_saver(context_saver_t *cs,
                    svn_stream_t *output_stream)
{
  int i;
  for (i = 0; i < cs->context_size; i++)
    {
      apr_size_t slot = (i + cs->next_slot) % cs->context_size;
      if (cs->data[slot])
        {
          apr_size_t len = cs->len[slot];
          SVN_ERR(svn_stream_write(output_stream, cs->data[slot], &len));
        }
    }
  return SVN_NO_ERROR;
}

static void
make_context_saver(svn_diff3__file_output_baton_t *fob)
{
  context_saver_t *cs;

  assert(fob->context_size > 0); /* Or nothing to save */

  svn_pool_clear(fob->pool);
  cs = apr_pcalloc(fob->pool, sizeof(*cs));
  cs->stream = svn_stream_empty(fob->pool);
  svn_stream_set_baton(cs->stream, cs);
  svn_stream_set_write(cs->stream, context_saver_stream_write);
  fob->context_saver = cs;
  fob->output_stream = cs->stream;
  cs->context_size = fob->context_size;
  cs->data = apr_pcalloc(fob->pool, sizeof(*cs->data) * cs->context_size);
  cs->len = apr_pcalloc(fob->pool, sizeof(*cs->len) * cs->context_size);
}


/* A stream which prints LINES_TO_PRINT (based on context size) lines to
   BATON->REAL_OUTPUT_STREAM, and then changes BATON->OUTPUT_STREAM to
   a context_saver; used for *trailing* context. */

struct trailing_context_printer {
  apr_size_t lines_to_print;
  svn_diff3__file_output_baton_t *fob;
};



static svn_error_t *
trailing_context_printer_write(void *baton,
                               const char *data,
                               apr_size_t *len)
{
  struct trailing_context_printer *tcp = baton;
  SVN_ERR_ASSERT(tcp->lines_to_print > 0);
  SVN_ERR(svn_stream_write(tcp->fob->real_output_stream, data, len));
  tcp->lines_to_print--;
  if (tcp->lines_to_print == 0)
    make_context_saver(tcp->fob);
  return SVN_NO_ERROR;
}


static void
make_trailing_context_printer(svn_diff3__file_output_baton_t *btn)
{
  struct trailing_context_printer *tcp;
  svn_stream_t *s;

  svn_pool_clear(btn->pool);

  tcp = apr_pcalloc(btn->pool, sizeof(*tcp));
  tcp->lines_to_print = btn->context_size;
  tcp->fob = btn;
  s = svn_stream_empty(btn->pool);
  svn_stream_set_baton(s, tcp);
  svn_stream_set_write(s, trailing_context_printer_write);
  btn->output_stream = s;
}



typedef enum svn_diff3__file_output_type_e
{
  svn_diff3__file_output_skip,
  svn_diff3__file_output_normal
} svn_diff3__file_output_type_e;


static svn_error_t *
output_line(svn_diff3__file_output_baton_t *baton,
            svn_diff3__file_output_type_e type, int idx)
{
  char *curp;
  char *endp;
  char *eol;
  apr_size_t len;

  curp = baton->curp[idx];
  endp = baton->endp[idx];

  /* Lazily update the current line even if we're at EOF.
   */
  baton->current_line[idx]++;

  if (curp == endp)
    return SVN_NO_ERROR;

  eol = svn_eol__find_eol_start(curp, endp - curp);
  if (!eol)
    eol = endp;
  else
    {
      svn_boolean_t had_cr = (*eol == '\r');
      eol++;
      if (had_cr && eol != endp && *eol == '\n')
        eol++;
    }

  if (type != svn_diff3__file_output_skip)
    {
      len = eol - curp;
      /* Note that the trailing context printer assumes that
         svn_stream_write is called exactly once per line. */
      SVN_ERR(svn_stream_write(baton->output_stream, curp, &len));
    }

  baton->curp[idx] = eol;

  return SVN_NO_ERROR;
}

static svn_error_t *
output_marker_eol(svn_diff3__file_output_baton_t *btn)
{
  return svn_stream_puts(btn->output_stream, btn->marker_eol);
}

static svn_error_t *
output_hunk(void *baton, int idx, apr_off_t target_line,
            apr_off_t target_length)
{
  svn_diff3__file_output_baton_t *output_baton = baton;

  /* Skip lines until we are at the start of the changed range */
  while (output_baton->current_line[idx] < target_line)
    {
      SVN_ERR(output_line(output_baton, svn_diff3__file_output_skip, idx));
    }

  target_line += target_length;

  while (output_baton->current_line[idx] < target_line)
    {
      SVN_ERR(output_line(output_baton, svn_diff3__file_output_normal, idx));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
output_common(void *baton, apr_off_t original_start, apr_off_t original_length,
              apr_off_t modified_start, apr_off_t modified_length,
              apr_off_t latest_start, apr_off_t latest_length)
{
  return output_hunk(baton, 1, modified_start, modified_length);
}

static svn_error_t *
output_diff_modified(void *baton,
                     apr_off_t original_start, apr_off_t original_length,
                     apr_off_t modified_start, apr_off_t modified_length,
                     apr_off_t latest_start, apr_off_t latest_length)
{
  return output_hunk(baton, 1, modified_start, modified_length);
}

static svn_error_t *
output_diff_latest(void *baton,
                   apr_off_t original_start, apr_off_t original_length,
                   apr_off_t modified_start, apr_off_t modified_length,
                   apr_off_t latest_start, apr_off_t latest_length)
{
  return output_hunk(baton, 2, latest_start, latest_length);
}

static svn_error_t *
output_conflict(void *baton,
                apr_off_t original_start, apr_off_t original_length,
                apr_off_t modified_start, apr_off_t modified_length,
                apr_off_t latest_start, apr_off_t latest_length,
                svn_diff_t *diff);

static const svn_diff_output_fns_t svn_diff3__file_output_vtable =
{
  output_common,
  output_diff_modified,
  output_diff_latest,
  output_diff_modified, /* output_diff_common */
  output_conflict
};

static svn_error_t *
output_conflict_with_context_marker(svn_diff3__file_output_baton_t *btn,
                                    const char *label,
                                    apr_off_t start,
                                    apr_off_t length)
{
  if (length == 1)
    SVN_ERR(svn_stream_printf(btn->output_stream, btn->pool,
                              "%s (%" APR_OFF_T_FMT ")",
                              label, start + 1));
  else
    SVN_ERR(svn_stream_printf(btn->output_stream, btn->pool,
                              "%s (%" APR_OFF_T_FMT ",%" APR_OFF_T_FMT ")",
                              label, start + 1, length));

  SVN_ERR(output_marker_eol(btn));

  return SVN_NO_ERROR;
}

static svn_error_t *
output_conflict_with_context(svn_diff3__file_output_baton_t *btn,
                             apr_off_t original_start,
                             apr_off_t original_length,
                             apr_off_t modified_start,
                             apr_off_t modified_length,
                             apr_off_t latest_start,
                             apr_off_t latest_length)
{
  /* Are we currently saving starting context (as opposed to printing
     trailing context)?  If so, flush it. */
  if (btn->output_stream == btn->context_saver->stream)
    {
      if (btn->context_saver->total_writes > btn->context_size)
        SVN_ERR(svn_stream_puts(btn->real_output_stream, "@@\n"));
      SVN_ERR(flush_context_saver(btn->context_saver, btn->real_output_stream));
    }

  /* Print to the real output stream. */
  btn->output_stream = btn->real_output_stream;

  /* Output the conflict itself. */
  SVN_ERR(output_conflict_with_context_marker(btn, btn->conflict_modified,
                                              modified_start, modified_length));
  SVN_ERR(output_hunk(btn, 1/*modified*/, modified_start, modified_length));

  SVN_ERR(output_conflict_with_context_marker(btn, btn->conflict_original,
                                              original_start, original_length));
  SVN_ERR(output_hunk(btn, 0/*original*/, original_start, original_length));

  SVN_ERR(svn_stream_printf(btn->output_stream, btn->pool,
                            "%s%s", btn->conflict_separator, btn->marker_eol));
  SVN_ERR(output_hunk(btn, 2/*latest*/, latest_start, latest_length));
  SVN_ERR(output_conflict_with_context_marker(btn, btn->conflict_latest,
                                              latest_start, latest_length));

  /* Go into print-trailing-context mode instead. */
  make_trailing_context_printer(btn);

  return SVN_NO_ERROR;
}


static svn_error_t *
output_conflict(void *baton,
                apr_off_t original_start, apr_off_t original_length,
                apr_off_t modified_start, apr_off_t modified_length,
                apr_off_t latest_start, apr_off_t latest_length,
                svn_diff_t *diff)
{
  svn_diff3__file_output_baton_t *file_baton = baton;

  svn_diff_conflict_display_style_t style = file_baton->conflict_style;

  if (style == svn_diff_conflict_display_only_conflicts)
    return output_conflict_with_context(file_baton,
                                        original_start, original_length,
                                        modified_start, modified_length,
                                        latest_start, latest_length);

  if (style == svn_diff_conflict_display_resolved_modified_latest)
    {
      if (diff)
        return svn_diff_output2(diff, baton,
                                &svn_diff3__file_output_vtable,
                                file_baton->cancel_func,
                                file_baton->cancel_baton);
      else
        style = svn_diff_conflict_display_modified_latest;
    }

  if (style == svn_diff_conflict_display_modified_latest ||
      style == svn_diff_conflict_display_modified_original_latest)
    {
      SVN_ERR(svn_stream_puts(file_baton->output_stream,
                               file_baton->conflict_modified));
      SVN_ERR(output_marker_eol(file_baton));

      SVN_ERR(output_hunk(baton, 1, modified_start, modified_length));

      if (style == svn_diff_conflict_display_modified_original_latest)
        {
          SVN_ERR(svn_stream_puts(file_baton->output_stream,
                                   file_baton->conflict_original));
          SVN_ERR(output_marker_eol(file_baton));
          SVN_ERR(output_hunk(baton, 0, original_start, original_length));
        }

      SVN_ERR(svn_stream_puts(file_baton->output_stream,
                              file_baton->conflict_separator));
      SVN_ERR(output_marker_eol(file_baton));

      SVN_ERR(output_hunk(baton, 2, latest_start, latest_length));

      SVN_ERR(svn_stream_puts(file_baton->output_stream,
                              file_baton->conflict_latest));
      SVN_ERR(output_marker_eol(file_baton));
    }
  else if (style == svn_diff_conflict_display_modified)
    SVN_ERR(output_hunk(baton, 1, modified_start, modified_length));
  else if (style == svn_diff_conflict_display_latest)
    SVN_ERR(output_hunk(baton, 2, latest_start, latest_length));
  else /* unknown style */
    SVN_ERR_MALFUNCTION();

  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff_file_output_merge3(svn_stream_t *output_stream,
                            svn_diff_t *diff,
                            const char *original_path,
                            const char *modified_path,
                            const char *latest_path,
                            const char *conflict_original,
                            const char *conflict_modified,
                            const char *conflict_latest,
                            const char *conflict_separator,
                            svn_diff_conflict_display_style_t style,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *scratch_pool)
{
  svn_diff3__file_output_baton_t baton;
  apr_file_t *file[3];
  int idx;
#if APR_HAS_MMAP
  apr_mmap_t *mm[3] = { 0 };
#endif /* APR_HAS_MMAP */
  const char *eol;
  svn_boolean_t conflicts_only =
    (style == svn_diff_conflict_display_only_conflicts);

  memset(&baton, 0, sizeof(baton));
  baton.context_size = SVN_DIFF__UNIFIED_CONTEXT_SIZE;
  if (conflicts_only)
    {
      baton.pool = svn_pool_create(scratch_pool);
      make_context_saver(&baton);
      baton.real_output_stream = output_stream;
    }
  else
    baton.output_stream = output_stream;
  baton.path[0] = original_path;
  baton.path[1] = modified_path;
  baton.path[2] = latest_path;
  SVN_ERR(svn_utf_cstring_from_utf8(&baton.conflict_modified,
                                    conflict_modified ? conflict_modified
                                    : apr_psprintf(scratch_pool, "<<<<<<< %s",
                                                   modified_path),
                                    scratch_pool));
  SVN_ERR(svn_utf_cstring_from_utf8(&baton.conflict_original,
                                    conflict_original ? conflict_original
                                    : apr_psprintf(scratch_pool, "||||||| %s",
                                                   original_path),
                                    scratch_pool));
  SVN_ERR(svn_utf_cstring_from_utf8(&baton.conflict_separator,
                                    conflict_separator ? conflict_separator
                                    : "=======", scratch_pool));
  SVN_ERR(svn_utf_cstring_from_utf8(&baton.conflict_latest,
                                    conflict_latest ? conflict_latest
                                    : apr_psprintf(scratch_pool, ">>>>>>> %s",
                                                   latest_path),
                                    scratch_pool));

  baton.conflict_style = style;

  for (idx = 0; idx < 3; idx++)
    {
      apr_size_t size;

      SVN_ERR(map_or_read_file(&file[idx],
                               MMAP_T_ARG(mm[idx])
                               &baton.buffer[idx], &size,
                               baton.path[idx], scratch_pool));

      baton.curp[idx] = baton.buffer[idx];
      baton.endp[idx] = baton.buffer[idx];

      if (baton.endp[idx])
        baton.endp[idx] += size;
    }

  /* Check what eol marker we should use for conflict markers.
     We use the eol marker of the modified file and fall back on the
     platform's eol marker if that file doesn't contain any newlines. */
  eol = svn_eol__detect_eol(baton.buffer[1], baton.endp[1] - baton.buffer[1],
                            NULL);
  if (! eol)
    eol = APR_EOL_STR;
  baton.marker_eol = eol;

  baton.cancel_func = cancel_func;
  baton.cancel_baton = cancel_baton;

  SVN_ERR(svn_diff_output2(diff, &baton,
                          &svn_diff3__file_output_vtable,
                          cancel_func, cancel_baton));

  for (idx = 0; idx < 3; idx++)
    {
#if APR_HAS_MMAP
      if (mm[idx])
        {
          apr_status_t rv = apr_mmap_delete(mm[idx]);
          if (rv != APR_SUCCESS)
            {
              return svn_error_wrap_apr(rv, _("Failed to delete mmap '%s'"),
                                        baton.path[idx]);
            }
        }
#endif /* APR_HAS_MMAP */

      if (file[idx])
        {
          SVN_ERR(svn_io_file_close(file[idx], scratch_pool));
        }
    }

  if (conflicts_only)
    svn_pool_destroy(baton.pool);

  return SVN_NO_ERROR;
}

