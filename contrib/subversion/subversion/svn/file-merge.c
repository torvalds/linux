/*
 * file-merge.c: internal file merge tool
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

/* This is an interactive file merge tool with an interface similar to
 * the interactive mode of the UNIX sdiff ("side-by-side diff") utility.
 * The merge tool is driven by Subversion's diff code and user input. */

#include "svn_cmdline.h"
#include "svn_dirent_uri.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_io.h"
#include "svn_utf.h"
#include "svn_xml.h"

#include "cl.h"

#include "svn_private_config.h"
#include "private/svn_utf_private.h"
#include "private/svn_cmdline_private.h"
#include "private/svn_dep_compat.h"

#if APR_HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#if APR_HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <fcntl.h>
#include <stdlib.h>

#if defined(HAVE_TERMIOS_H)
#include <termios.h>
#endif

/* Baton for functions in this file which implement svn_diff_output_fns_t. */
struct file_merge_baton {
  /* The files being merged. */
  apr_file_t *original_file;
  apr_file_t *modified_file;
  apr_file_t *latest_file;

  /* Counters to keep track of the current line in each file. */
  svn_linenum_t current_line_original;
  svn_linenum_t current_line_modified;
  svn_linenum_t current_line_latest;

  /* The merge result is written to this file. */
  apr_file_t *merged_file;

  /* Whether the merged file remains in conflict after the merge. */
  svn_boolean_t remains_in_conflict;

  /* External editor command for editing chunks. */
  const char *editor_cmd;

  /* The client configuration hash. */
  apr_hash_t *config;

  /* Whether the merge should be aborted. */
  svn_boolean_t abort_merge;

  /* Pool for temporary allocations. */
  apr_pool_t *scratch_pool;
};

/* Copy LEN lines from SOURCE_FILE to the MERGED_FILE, starting at
 * line START. The CURRENT_LINE is the current line in the source file.
 * The new current line is returned in *NEW_CURRENT_LINE. */
static svn_error_t *
copy_to_merged_file(svn_linenum_t *new_current_line,
                    apr_file_t *merged_file,
                    apr_file_t *source_file,
                    apr_off_t start,
                    apr_off_t len,
                    svn_linenum_t current_line,
                    apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  svn_stringbuf_t *line;
  apr_size_t lines_read;
  apr_size_t lines_copied;
  svn_boolean_t eof;
  svn_linenum_t orig_current_line = current_line;

  lines_read = 0;
  iterpool = svn_pool_create(scratch_pool);
  while (current_line < start)
    {
      svn_pool_clear(iterpool);

      SVN_ERR(svn_io_file_readline(source_file, &line, NULL, &eof,
                                   APR_SIZE_MAX, iterpool, iterpool));
      if (eof)
        break;

      current_line++;
      lines_read++;
    }

  lines_copied = 0;
  while (lines_copied < len)
    {
      apr_size_t bytes_written;
      const char *eol_str;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_io_file_readline(source_file, &line, &eol_str, &eof,
                                   APR_SIZE_MAX, iterpool, iterpool));
      if (eol_str)
        svn_stringbuf_appendcstr(line, eol_str);
      SVN_ERR(svn_io_file_write_full(merged_file, line->data, line->len,
                                     &bytes_written, iterpool));
      if (bytes_written != line->len)
        return svn_error_create(SVN_ERR_IO_WRITE_ERROR, NULL,
                                _("Could not write data to merged file"));
      if (eof)
        break;
      lines_copied++;
    }
  svn_pool_destroy(iterpool);

  *new_current_line = orig_current_line + lines_read + lines_copied;

  return SVN_NO_ERROR;
}

/* Copy common data to the merged file. */
static svn_error_t *
file_merge_output_common(void *output_baton,
                         apr_off_t original_start,
                         apr_off_t original_length,
                         apr_off_t modified_start,
                         apr_off_t modified_length,
                         apr_off_t latest_start,
                         apr_off_t latest_length)
{
  struct file_merge_baton *b = output_baton;

  if (b->abort_merge)
    return SVN_NO_ERROR;

  SVN_ERR(copy_to_merged_file(&b->current_line_original,
                              b->merged_file,
                              b->original_file,
                              original_start,
                              original_length,
                              b->current_line_original,
                              b->scratch_pool));
  return SVN_NO_ERROR;
}

/* Original/latest match up, but modified differs.
 * Copy modified data to the merged file. */
static svn_error_t *
file_merge_output_diff_modified(void *output_baton,
                                apr_off_t original_start,
                                apr_off_t original_length,
                                apr_off_t modified_start,
                                apr_off_t modified_length,
                                apr_off_t latest_start,
                                apr_off_t latest_length)
{
  struct file_merge_baton *b = output_baton;

  if (b->abort_merge)
    return SVN_NO_ERROR;

  SVN_ERR(copy_to_merged_file(&b->current_line_modified,
                              b->merged_file,
                              b->modified_file,
                              modified_start,
                              modified_length,
                              b->current_line_modified,
                              b->scratch_pool));

  return SVN_NO_ERROR;
}

/* Original/modified match up, but latest differs.
 * Copy latest data to the merged file. */
static svn_error_t *
file_merge_output_diff_latest(void *output_baton,
                              apr_off_t original_start,
                              apr_off_t original_length,
                              apr_off_t modified_start,
                              apr_off_t modified_length,
                              apr_off_t latest_start,
                              apr_off_t latest_length)
{
  struct file_merge_baton *b = output_baton;

  if (b->abort_merge)
    return SVN_NO_ERROR;

  SVN_ERR(copy_to_merged_file(&b->current_line_latest,
                              b->merged_file,
                              b->latest_file,
                              latest_start,
                              latest_length,
                              b->current_line_latest,
                              b->scratch_pool));

  return SVN_NO_ERROR;
}

/* Modified/latest match up, but original differs.
 * Copy latest data to the merged file. */
static svn_error_t *
file_merge_output_diff_common(void *output_baton,
                              apr_off_t original_start,
                              apr_off_t original_length,
                              apr_off_t modified_start,
                              apr_off_t modified_length,
                              apr_off_t latest_start,
                              apr_off_t latest_length)
{
  struct file_merge_baton *b = output_baton;

  if (b->abort_merge)
    return SVN_NO_ERROR;

  SVN_ERR(copy_to_merged_file(&b->current_line_latest,
                              b->merged_file,
                              b->latest_file,
                              latest_start,
                              latest_length,
                              b->current_line_latest,
                              b->scratch_pool));
  return SVN_NO_ERROR;
}


/* Return LEN lines within the diff chunk staring at line START
 * in a *LINES array of svn_stringbuf_t* elements.
 * Store the resulting current in in *NEW_CURRENT_LINE. */
static svn_error_t *
read_diff_chunk(apr_array_header_t **lines,
                svn_linenum_t *new_current_line,
                apr_file_t *file,
                svn_linenum_t current_line,
                apr_off_t start,
                apr_off_t len,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *line;
  const char *eol_str;
  svn_boolean_t eof;
  apr_pool_t *iterpool;

  *lines = apr_array_make(result_pool, 0, sizeof(svn_stringbuf_t *));

  /* Skip lines before start of range. */
  iterpool = svn_pool_create(scratch_pool);
  while (current_line < start)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(svn_io_file_readline(file, &line, NULL, &eof, APR_SIZE_MAX,
                                   iterpool, iterpool));
      if (eof)
        return SVN_NO_ERROR;
      current_line++;
    }
  svn_pool_destroy(iterpool);

  /* Now read the lines. */
  do
    {
      SVN_ERR(svn_io_file_readline(file, &line, &eol_str, &eof, APR_SIZE_MAX,
                                   result_pool, scratch_pool));
      if (eol_str)
        svn_stringbuf_appendcstr(line, eol_str);
      APR_ARRAY_PUSH(*lines, svn_stringbuf_t *) = line;
      if (eof)
        break;
      current_line++;
    }
  while ((*lines)->nelts < len);

  *new_current_line = current_line;

  return SVN_NO_ERROR;
}

/* Return the terminal width in number of columns. */
static int
get_term_width(void)
{
  char *columns_env;
#ifdef TIOCGWINSZ
  int fd;

  fd = open("/dev/tty", O_RDONLY, 0);
  if (fd != -1)
    {
      struct winsize ws;
      int error;

      error = ioctl(fd, TIOCGWINSZ, &ws);
      close(fd);
      if (error != -1)
        {
          if (ws.ws_col < 80)
            return 80;
          return ws.ws_col;
        }
    }
#elif defined WIN32
  CONSOLE_SCREEN_BUFFER_INFO csbi;

  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
    {
      if (csbi.dwSize.X < 80)
        return 80;
      return csbi.dwSize.X;
    }
#endif

  columns_env = getenv("COLUMNS");
  if (columns_env)
    {
      svn_error_t *err;
      int cols;

      err = svn_cstring_atoi(&cols, columns_env);
      if (err)
        {
          svn_error_clear(err);
          return 80;
        }

      if (cols < 80)
        return 80;
      return cols;
    }
  else
    return 80;
}

#define LINE_DISPLAY_WIDTH ((get_term_width() / 2) - 2)

/* Prepare LINE for display, pruning or extending it to an appropriate
 * display width, and stripping the EOL marker, if any.
 * This function assumes that the data in LINE is encoded in UTF-8. */
static const char *
prepare_line_for_display(const char *line, apr_pool_t *pool)
{
  svn_stringbuf_t *buf = svn_stringbuf_create(line, pool);
  size_t width;
  size_t line_width = LINE_DISPLAY_WIDTH;
  apr_pool_t *iterpool;

  /* Trim EOL. */
  if (buf->len >= 2 &&
      buf->data[buf->len - 2] == '\r' &&
      buf->data[buf->len - 1] == '\n')
    svn_stringbuf_chop(buf, 2);
  else if (buf->len >= 1 &&
           (buf->data[buf->len - 1] == '\n' ||
            buf->data[buf->len - 1] == '\r'))
    svn_stringbuf_chop(buf, 1);

  /* Determine the on-screen width of the line. */
  width = svn_utf_cstring_utf8_width(buf->data);
  if (width == -1)
    {
      /* Determining the width failed. Try to get rid of unprintable
       * characters in the line buffer. */
      buf = svn_stringbuf_create(svn_xml_fuzzy_escape(buf->data, pool), pool);
      width = svn_utf_cstring_utf8_width(buf->data);
      if (width == -1)
        width = buf->len; /* fallback: buffer length */
    }

  /* Trim further in case line is still too long, or add padding in case
   * it is too short. */
  iterpool = svn_pool_create(pool);
  while (width > line_width)
    {
      const char *last_valid;

      svn_pool_clear(iterpool);

      svn_stringbuf_chop(buf, 1);

      /* Be careful not to invalidate the UTF-8 string by trimming
       * just part of a character. */
      last_valid = svn_utf__last_valid(buf->data, buf->len);
      if (last_valid < buf->data + buf->len)
        svn_stringbuf_chop(buf, (buf->data + buf->len) - last_valid);

      width = svn_utf_cstring_utf8_width(buf->data);
      if (width == -1)
        width = buf->len; /* fallback: buffer length */
    }
  svn_pool_destroy(iterpool);

  while (width == 0 || width < line_width)
    {
      svn_stringbuf_appendbyte(buf, ' ');
      width++;
    }

  SVN_ERR_ASSERT_NO_RETURN(width == line_width);
  return buf->data;
}

/* Merge CHUNK1 and CHUNK2 into a new chunk with conflict markers. */
static apr_array_header_t *
merge_chunks_with_conflict_markers(apr_array_header_t *chunk1,
                                   apr_array_header_t *chunk2,
                                   apr_pool_t *result_pool)
{
  apr_array_header_t *merged_chunk;
  int i;

  merged_chunk = apr_array_make(result_pool, 0, sizeof(svn_stringbuf_t *));
  /* ### would be nice to show filenames next to conflict markers */
  APR_ARRAY_PUSH(merged_chunk, svn_stringbuf_t *) =
    svn_stringbuf_create("<<<<<<<\n", result_pool);
  for (i = 0; i < chunk1->nelts; i++)
    {
      APR_ARRAY_PUSH(merged_chunk, svn_stringbuf_t *) =
        APR_ARRAY_IDX(chunk1, i, svn_stringbuf_t*);
    }
  APR_ARRAY_PUSH(merged_chunk, svn_stringbuf_t *) =
    svn_stringbuf_create("=======\n", result_pool);
  for (i = 0; i < chunk2->nelts; i++)
    {
      APR_ARRAY_PUSH(merged_chunk, svn_stringbuf_t *) =
        APR_ARRAY_IDX(chunk2, i, svn_stringbuf_t*);
    }
  APR_ARRAY_PUSH(merged_chunk, svn_stringbuf_t *) =
    svn_stringbuf_create(">>>>>>>\n", result_pool);

  return merged_chunk;
}

/* Edit CHUNK and return the result in *MERGED_CHUNK allocated in POOL. */
static svn_error_t *
edit_chunk(apr_array_header_t **merged_chunk,
           apr_array_header_t *chunk,
           const char *editor_cmd,
           apr_hash_t *config,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool)
{
  apr_file_t *temp_file;
  const char *temp_file_name;
  int i;
  apr_off_t pos;
  svn_boolean_t eof;
  svn_error_t *err;
  apr_pool_t *iterpool;

  SVN_ERR(svn_io_open_unique_file3(&temp_file, &temp_file_name, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   scratch_pool, scratch_pool));
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < chunk->nelts; i++)
    {
      svn_stringbuf_t *line = APR_ARRAY_IDX(chunk, i, svn_stringbuf_t *);
      apr_size_t bytes_written;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_io_file_write_full(temp_file, line->data, line->len,
                                     &bytes_written, iterpool));
      if (line->len != bytes_written)
        return svn_error_create(SVN_ERR_IO_WRITE_ERROR, NULL,
                                _("Could not write data to temporary file"));
    }
  SVN_ERR(svn_io_file_flush(temp_file, scratch_pool));

  err = svn_cmdline__edit_file_externally(temp_file_name, editor_cmd,
                                          config, scratch_pool);
  if (err && (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_EDITOR))
    {
      svn_error_t *root_err = svn_error_root_cause(err);

      SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "%s\n",
                                  root_err->message ? root_err->message :
                                  _("No editor found.")));
      svn_error_clear(err);
      *merged_chunk = NULL;
      svn_pool_destroy(iterpool);
      return SVN_NO_ERROR;
    }
  else if (err && (err->apr_err == SVN_ERR_EXTERNAL_PROGRAM))
    {
      svn_error_t *root_err = svn_error_root_cause(err);

      SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "%s\n",
                                  root_err->message ? root_err->message :
                                  _("Error running editor.")));
      svn_error_clear(err);
      *merged_chunk = NULL;
      svn_pool_destroy(iterpool);
      return SVN_NO_ERROR;
    }
  else if (err)
    return svn_error_trace(err);

  *merged_chunk = apr_array_make(result_pool, 1, sizeof(svn_stringbuf_t *));
  pos = 0;
  SVN_ERR(svn_io_file_seek(temp_file, APR_SET, &pos, scratch_pool));
  do
    {
      svn_stringbuf_t *line;
      const char *eol_str;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_io_file_readline(temp_file, &line, &eol_str, &eof,
                                   APR_SIZE_MAX, result_pool, iterpool));
      if (eol_str)
        svn_stringbuf_appendcstr(line, eol_str);

      APR_ARRAY_PUSH(*merged_chunk, svn_stringbuf_t *) = line;
    }
  while (!eof);
  svn_pool_destroy(iterpool);

  SVN_ERR(svn_io_file_close(temp_file, scratch_pool));

  return SVN_NO_ERROR;
}

/* Create a separator string of the appropriate length. */
static const char *
get_sep_string(apr_pool_t *result_pool)
{
  int line_width = LINE_DISPLAY_WIDTH;
  int i;
  svn_stringbuf_t *buf;

  buf = svn_stringbuf_create_empty(result_pool);
  for (i = 0; i < line_width; i++)
    svn_stringbuf_appendbyte(buf, '-');
  svn_stringbuf_appendbyte(buf, '+');
  for (i = 0; i < line_width; i++)
    svn_stringbuf_appendbyte(buf, '-');
  svn_stringbuf_appendbyte(buf, '\n');

  return buf->data;
}

/* Merge chunks CHUNK1 and CHUNK2.
 * Each lines array contains elements of type svn_stringbuf_t*.
 * Return the result in *MERGED_CHUNK, or set *MERGED_CHUNK to NULL in
 * case the user chooses to postpone resolution of this chunk.
 * If the user wants to abort the merge, set *ABORT_MERGE to TRUE. */
static svn_error_t *
merge_chunks(apr_array_header_t **merged_chunk,
             svn_boolean_t *abort_merge,
             apr_array_header_t *chunk1,
             apr_array_header_t *chunk2,
             svn_linenum_t current_line1,
             svn_linenum_t current_line2,
             const char *editor_cmd,
             apr_hash_t *config,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *prompt;
  int i;
  int max_chunk_lines;
  apr_pool_t *iterpool;

  max_chunk_lines = chunk1->nelts > chunk2->nelts ? chunk1->nelts
                                                  : chunk2->nelts;
  *abort_merge = FALSE;

  /*
   * Prepare the selection prompt.
   */

  prompt = svn_stringbuf_create(
             apr_psprintf(scratch_pool, "%s\n%s|%s\n%s",
                          _("Conflicting section found during merge:"),
                          prepare_line_for_display(
                            apr_psprintf(scratch_pool,
                                         _("(1) their version (at line %lu)"),
                                         current_line1),
                            scratch_pool),
                          prepare_line_for_display(
                            apr_psprintf(scratch_pool,
                                         _("(2) your version (at line %lu)"),
                                         current_line2),
                            scratch_pool),
                          get_sep_string(scratch_pool)),
             scratch_pool);

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < max_chunk_lines; i++)
    {
      const char *line1;
      const char *line2;
      const char *prompt_line;

      svn_pool_clear(iterpool);

      if (i < chunk1->nelts)
        {
          svn_stringbuf_t *line_utf8;

          SVN_ERR(svn_utf_stringbuf_to_utf8(&line_utf8,
                                            APR_ARRAY_IDX(chunk1, i,
                                                          svn_stringbuf_t*),
                                            iterpool));
          line1 = prepare_line_for_display(line_utf8->data, iterpool);
        }
      else
        line1 = prepare_line_for_display("", iterpool);

      if (i < chunk2->nelts)
        {
          svn_stringbuf_t *line_utf8;

          SVN_ERR(svn_utf_stringbuf_to_utf8(&line_utf8,
                                            APR_ARRAY_IDX(chunk2, i,
                                                          svn_stringbuf_t*),
                                            iterpool));
          line2 = prepare_line_for_display(line_utf8->data, iterpool);
        }
      else
        line2 = prepare_line_for_display("", iterpool);

      prompt_line = apr_psprintf(iterpool, "%s|%s\n", line1, line2);

      svn_stringbuf_appendcstr(prompt, prompt_line);
    }

  svn_stringbuf_appendcstr(prompt, get_sep_string(scratch_pool));
  svn_stringbuf_appendcstr(
    prompt,
    _("Select: (1) use their version, (2) use your version,\n"
      "        (12) their version first, then yours,\n"
      "        (21) your version first, then theirs,\n"
      "        (e1) edit their version and use the result,\n"
      "        (e2) edit your version and use the result,\n"
      "        (eb) edit both versions and use the result,\n"
      "        (p) postpone this conflicting section leaving conflict markers,\n"
      "        (a) abort file merge and return to main menu: "));

  /* Now let's see what the user wants to do with this conflict. */
  while (TRUE)
    {
      const char *answer;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_cmdline_prompt_user2(&answer, prompt->data, NULL, iterpool));
      if (strcmp(answer, "1") == 0)
        {
          *merged_chunk = chunk1;
          break;
        }
      else if (strcmp(answer, "2") == 0)
        {
          *merged_chunk = chunk2;
          break;
        }
      if (strcmp(answer, "12") == 0)
        {
          *merged_chunk = apr_array_make(result_pool,
                                         chunk1->nelts + chunk2->nelts,
                                         sizeof(svn_stringbuf_t *));
          apr_array_cat(*merged_chunk, chunk1);
          apr_array_cat(*merged_chunk, chunk2);
          break;
        }
      if (strcmp(answer, "21") == 0)
        {
          *merged_chunk = apr_array_make(result_pool,
                                         chunk1->nelts + chunk2->nelts,
                                         sizeof(svn_stringbuf_t *));
          apr_array_cat(*merged_chunk, chunk2);
          apr_array_cat(*merged_chunk, chunk1);
          break;
        }
      else if (strcmp(answer, "p") == 0)
        {
          *merged_chunk = NULL;
          break;
        }
      else if (strcmp(answer, "e1") == 0)
        {
          SVN_ERR(edit_chunk(merged_chunk, chunk1, editor_cmd, config,
                             result_pool, iterpool));
          if (*merged_chunk)
            break;
        }
      else if (strcmp(answer, "e2") == 0)
        {
          SVN_ERR(edit_chunk(merged_chunk, chunk2, editor_cmd, config,
                             result_pool, iterpool));
          if (*merged_chunk)
            break;
        }
      else if (strcmp(answer, "eb") == 0)
        {
          apr_array_header_t *conflict_chunk;

          conflict_chunk = merge_chunks_with_conflict_markers(chunk1, chunk2,
                                                              scratch_pool);
          SVN_ERR(edit_chunk(merged_chunk, conflict_chunk, editor_cmd, config,
                             result_pool, iterpool));
          if (*merged_chunk)
            break;
        }
      else if (strcmp(answer, "a") == 0)
        {
          *abort_merge = TRUE;
          break;
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Perform a merge of chunks from FILE1 and FILE2, specified by START1/LEN1
 * and START2/LEN2, respectively. Append the result to MERGED_FILE.
 * The current line numbers for FILE1 and FILE2 are passed in *CURRENT_LINE1
 * and *CURRENT_LINE2, and will be updated to new values upon return.
 * If the user wants to abort the merge, set *ABORT_MERGE to TRUE. */
static svn_error_t *
merge_file_chunks(svn_boolean_t *remains_in_conflict,
                  svn_boolean_t *abort_merge,
                  apr_file_t *merged_file,
                  apr_file_t *file1,
                  apr_file_t *file2,
                  apr_off_t start1,
                  apr_off_t len1,
                  apr_off_t start2,
                  apr_off_t len2,
                  svn_linenum_t *current_line1,
                  svn_linenum_t *current_line2,
                  const char *editor_cmd,
                  apr_hash_t *config,
                  apr_pool_t *scratch_pool)
{
  apr_array_header_t *chunk1;
  apr_array_header_t *chunk2;
  apr_array_header_t *merged_chunk;
  apr_pool_t *iterpool;
  int i;

  SVN_ERR(read_diff_chunk(&chunk1, current_line1, file1, *current_line1,
                          start1, len1, scratch_pool, scratch_pool));
  SVN_ERR(read_diff_chunk(&chunk2, current_line2, file2, *current_line2,
                          start2, len2, scratch_pool, scratch_pool));

  SVN_ERR(merge_chunks(&merged_chunk, abort_merge, chunk1, chunk2,
                       *current_line1, *current_line2,
                       editor_cmd, config,
                       scratch_pool, scratch_pool));

  if (*abort_merge)
      return SVN_NO_ERROR;

  /* If the user chose 'postpone' put conflict markers and left/right
   * versions into the merged file. */
  if (merged_chunk == NULL)
    {
      *remains_in_conflict = TRUE;
      merged_chunk = merge_chunks_with_conflict_markers(chunk1, chunk2,
                                                        scratch_pool);
    }

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < merged_chunk->nelts; i++)
    {
      apr_size_t bytes_written;
      svn_stringbuf_t *line = APR_ARRAY_IDX(merged_chunk, i,
                                            svn_stringbuf_t *);

      svn_pool_clear(iterpool);

      SVN_ERR(svn_io_file_write_full(merged_file, line->data, line->len,
                                     &bytes_written, iterpool));
      if (line->len != bytes_written)
        return svn_error_create(SVN_ERR_IO_WRITE_ERROR, NULL,
                                _("Could not write data to merged file"));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Original, modified, and latest all differ from one another.
 * This is a conflict and we'll need to ask the user to merge it. */
static svn_error_t *
file_merge_output_conflict(void *output_baton,
                           apr_off_t original_start,
                           apr_off_t original_length,
                           apr_off_t modified_start,
                           apr_off_t modified_length,
                           apr_off_t latest_start,
                           apr_off_t latest_length,
                           svn_diff_t *resolved_diff)
{
  struct file_merge_baton *b = output_baton;

  if (b->abort_merge)
    return SVN_NO_ERROR;

  SVN_ERR(merge_file_chunks(&b->remains_in_conflict,
                            &b->abort_merge,
                            b->merged_file,
                            b->modified_file,
                            b->latest_file,
                            modified_start,
                            modified_length,
                            latest_start,
                            latest_length,
                            &b->current_line_modified,
                            &b->current_line_latest,
                            b->editor_cmd,
                            b->config,
                            b->scratch_pool));
  return SVN_NO_ERROR;
}

/* Our collection of diff output functions that get driven during the merge. */
static svn_diff_output_fns_t file_merge_diff_output_fns = {
  file_merge_output_common,
  file_merge_output_diff_modified,
  file_merge_output_diff_latest,
  file_merge_output_diff_common,
  file_merge_output_conflict
};

svn_error_t *
svn_cl__merge_file(svn_boolean_t *remains_in_conflict,
                   const char *base_path,
                   const char *their_path,
                   const char *my_path,
                   const char *merged_path,
                   const char *wc_path,
                   const char *path_prefix,
                   const char *editor_cmd,
                   apr_hash_t *config,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *scratch_pool)
{
  svn_diff_t *diff;
  svn_diff_file_options_t *diff_options;
  apr_file_t *original_file;
  apr_file_t *modified_file;
  apr_file_t *latest_file;
  apr_file_t *merged_file;
  const char *merged_file_name;
  struct file_merge_baton fmb;
  svn_boolean_t executable;
  const char *merged_path_local_style;
  const char *merged_rel_path;
  const char *wc_path_local_style;
  const char *wc_rel_path = svn_dirent_skip_ancestor(path_prefix, wc_path);

  /* PATH_PREFIX may not be an ancestor of WC_PATH, just use the
     full WC_PATH in that case. */
  if (wc_rel_path)
    wc_path_local_style = svn_dirent_local_style(wc_rel_path, scratch_pool);
  else
    wc_path_local_style = svn_dirent_local_style(wc_path, scratch_pool);

  SVN_ERR(svn_cmdline_printf(scratch_pool, _("Merging '%s'.\n"),
                             wc_path_local_style));

  SVN_ERR(svn_io_file_open(&original_file, base_path,
                           APR_READ | APR_BUFFERED,
                           APR_OS_DEFAULT, scratch_pool));
  SVN_ERR(svn_io_file_open(&modified_file, their_path,
                           APR_READ | APR_BUFFERED,
                           APR_OS_DEFAULT, scratch_pool));
  SVN_ERR(svn_io_file_open(&latest_file, my_path,
                           APR_READ | APR_BUFFERED,
                           APR_OS_DEFAULT, scratch_pool));
  SVN_ERR(svn_io_open_unique_file3(&merged_file, &merged_file_name,
                                   NULL, svn_io_file_del_none,
                                   scratch_pool, scratch_pool));

  diff_options = svn_diff_file_options_create(scratch_pool);
  SVN_ERR(svn_diff_file_diff3_2(&diff, base_path, their_path, my_path,
                                diff_options, scratch_pool));

  fmb.original_file = original_file;
  fmb.modified_file = modified_file;
  fmb.latest_file = latest_file;
  fmb.current_line_original = 0;
  fmb.current_line_modified = 0;
  fmb.current_line_latest = 0;
  fmb.merged_file = merged_file;
  fmb.remains_in_conflict = FALSE;
  fmb.editor_cmd = editor_cmd;
  fmb.config = config;
  fmb.abort_merge = FALSE;
  fmb.scratch_pool = scratch_pool;

  SVN_ERR(svn_diff_output2(diff, &fmb, &file_merge_diff_output_fns,
                           cancel_func, cancel_baton));

  SVN_ERR(svn_io_file_close(original_file, scratch_pool));
  SVN_ERR(svn_io_file_close(modified_file, scratch_pool));
  SVN_ERR(svn_io_file_close(latest_file, scratch_pool));
  SVN_ERR(svn_io_file_close(merged_file, scratch_pool));

  /* Start out assuming that conflicts remain. */
  if (remains_in_conflict)
    *remains_in_conflict = TRUE;

  if (fmb.abort_merge)
    {
      SVN_ERR(svn_io_remove_file2(merged_file_name, TRUE, scratch_pool));
      SVN_ERR(svn_cmdline_printf(scratch_pool, _("Merge of '%s' aborted.\n"),
                                 wc_path_local_style));
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_io_is_file_executable(&executable, merged_path, scratch_pool));

  merged_rel_path = svn_dirent_skip_ancestor(path_prefix, merged_path);
  if (merged_rel_path)
    merged_path_local_style = svn_dirent_local_style(merged_rel_path,
                                                     scratch_pool);
  else
    merged_path_local_style = svn_dirent_local_style(merged_path,
                                                     scratch_pool);

  SVN_ERR_W(svn_io_copy_file(merged_file_name, merged_path, FALSE,
                             scratch_pool),
            apr_psprintf(scratch_pool,
                         _("Could not write merged result to '%s', saved "
                           "instead at '%s'.\n'%s' remains in conflict.\n"),
                         merged_path_local_style,
                         svn_dirent_local_style(merged_file_name,
                                                scratch_pool),
                         wc_path_local_style));
  SVN_ERR(svn_io_set_file_executable(merged_path, executable, FALSE,
                                     scratch_pool));
  SVN_ERR(svn_io_remove_file2(merged_file_name, TRUE, scratch_pool));

  /* The merge was not aborted and we could install the merged result. The
   * file remains in conflict unless all conflicting sections were resolved. */
  if (remains_in_conflict)
    *remains_in_conflict = fmb.remains_in_conflict;

  if (fmb.remains_in_conflict)
    SVN_ERR(svn_cmdline_printf(
              scratch_pool,
              _("Merge of '%s' completed (remains in conflict).\n"),
              wc_path_local_style));
  else
    SVN_ERR(svn_cmdline_printf(
              scratch_pool, _("Merge of '%s' completed.\n"),
              wc_path_local_style));

  return SVN_NO_ERROR;
}
