/*
 * util.c :  routines for doing diffs
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
#include <apr_general.h>

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_props.h"
#include "svn_mergeinfo.h"
#include "svn_error.h"
#include "svn_diff.h"
#include "svn_types.h"
#include "svn_ctype.h"
#include "svn_utf.h"
#include "svn_version.h"

#include "private/svn_diff_private.h"
#include "private/svn_sorts_private.h"
#include "diff.h"

#include "svn_private_config.h"


svn_boolean_t
svn_diff_contains_conflicts(svn_diff_t *diff)
{
  while (diff != NULL)
    {
      if (diff->type == svn_diff__type_conflict)
        {
          return TRUE;
        }

      diff = diff->next;
    }

  return FALSE;
}

svn_boolean_t
svn_diff_contains_diffs(svn_diff_t *diff)
{
  while (diff != NULL)
    {
      if (diff->type != svn_diff__type_common)
        {
          return TRUE;
        }

      diff = diff->next;
    }

  return FALSE;
}

svn_error_t *
svn_diff_output2(svn_diff_t *diff,
                 void *output_baton,
                 const svn_diff_output_fns_t *vtable,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton)
{
  svn_error_t *(*output_fn)(void *,
                            apr_off_t, apr_off_t,
                            apr_off_t, apr_off_t,
                            apr_off_t, apr_off_t);

  while (diff != NULL)
    {
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      switch (diff->type)
        {
        case svn_diff__type_common:
          output_fn = vtable->output_common;
          break;

        case svn_diff__type_diff_common:
          output_fn = vtable->output_diff_common;
          break;

        case svn_diff__type_diff_modified:
          output_fn = vtable->output_diff_modified;
          break;

        case svn_diff__type_diff_latest:
          output_fn = vtable->output_diff_latest;
          break;

        case svn_diff__type_conflict:
          output_fn = NULL;
          if (vtable->output_conflict != NULL)
            {
              SVN_ERR(vtable->output_conflict(output_baton,
                               diff->original_start, diff->original_length,
                               diff->modified_start, diff->modified_length,
                               diff->latest_start, diff->latest_length,
                               diff->resolved_diff));
            }
          break;

        default:
          output_fn = NULL;
          break;
        }

      if (output_fn != NULL)
        {
          SVN_ERR(output_fn(output_baton,
                            diff->original_start, diff->original_length,
                            diff->modified_start, diff->modified_length,
                            diff->latest_start, diff->latest_length));
        }

      diff = diff->next;
    }

  return SVN_NO_ERROR;
}


void
svn_diff__normalize_buffer(char **tgt,
                           apr_off_t *lengthp,
                           svn_diff__normalize_state_t *statep,
                           const char *buf,
                           const svn_diff_file_options_t *opts)
{
  /* Variables for looping through BUF */
  const char *curp, *endp;

  /* Variable to record normalizing state */
  svn_diff__normalize_state_t state = *statep;

  /* Variables to track what needs copying into the target buffer */
  const char *start = buf;
  apr_size_t include_len = 0;
  svn_boolean_t last_skipped = FALSE; /* makes sure we set 'start' */

  /* Variable to record the state of the target buffer */
  char *tgt_newend = *tgt;

  /* If this is a noop, then just get out of here. */
  if (! opts->ignore_space && ! opts->ignore_eol_style)
    {
      *tgt = (char *)buf;
      return;
    }


  /* It only took me forever to get this routine right,
     so here my thoughts go:

    Below, we loop through the data, doing 2 things:

     - Normalizing
     - Copying other data

     The routine tries its hardest *not* to copy data, but instead
     returning a pointer into already normalized existing data.

     To this end, a block 'other data' shouldn't be copied when found,
     but only as soon as it can't be returned in-place.

     On a character level, there are 3 possible operations:

     - Skip the character (don't include in the normalized data)
     - Include the character (do include in the normalizad data)
     - Include as another character
       This is essentially the same as skipping the current character
       and inserting a given character in the output data.

    The macros below (SKIP, INCLUDE and INCLUDE_AS) are defined to
    handle the character based operations.  The macros themselves
    collect character level data into blocks.

    At all times designate the START, INCLUDED_LEN and CURP pointers
    an included and and skipped block like this:

      [ start, start + included_len ) [ start + included_len, curp )
             INCLUDED                        EXCLUDED

    When the routine flips from skipping to including, the last
    included block has to be flushed to the output buffer.
  */

  /* Going from including to skipping; only schedules the current
     included section for flushing.
     Also, simply chop off the character if it's the first in the buffer,
     so we can possibly just return the remainder of the buffer */
#define SKIP             \
  do {                   \
    if (start == curp)   \
       ++start;          \
    last_skipped = TRUE; \
  } while (0)

#define INCLUDE                \
  do {                         \
    if (last_skipped)          \
      COPY_INCLUDED_SECTION;   \
    ++include_len;             \
    last_skipped = FALSE;      \
  } while (0)

#define COPY_INCLUDED_SECTION                     \
  do {                                            \
    if (include_len > 0)                          \
      {                                           \
         memmove(tgt_newend, start, include_len); \
         tgt_newend += include_len;               \
         include_len = 0;                         \
      }                                           \
    start = curp;                                 \
  } while (0)

  /* Include the current character as character X.
     If the current character already *is* X, add it to the
     currently included region, increasing chances for consecutive
     fully normalized blocks. */
#define INCLUDE_AS(x)          \
  do {                         \
    if (*curp == (x))          \
      INCLUDE;                 \
    else                       \
      {                        \
        INSERT((x));           \
        SKIP;                  \
      }                        \
  } while (0)

  /* Insert character X in the output buffer */
#define INSERT(x)              \
  do {                         \
    COPY_INCLUDED_SECTION;     \
    *tgt_newend++ = (x);       \
  } while (0)

  for (curp = buf, endp = buf + *lengthp; curp != endp; ++curp)
    {
      switch (*curp)
        {
        case '\r':
          if (opts->ignore_eol_style)
            INCLUDE_AS('\n');
          else
            INCLUDE;
          state = svn_diff__normalize_state_cr;
          break;

        case '\n':
          if (state == svn_diff__normalize_state_cr
              && opts->ignore_eol_style)
            SKIP;
          else
            INCLUDE;
          state = svn_diff__normalize_state_normal;
          break;

        default:
          if (svn_ctype_isspace(*curp)
              && opts->ignore_space != svn_diff_file_ignore_space_none)
            {
              /* Whitespace but not '\r' or '\n' */
              if (state != svn_diff__normalize_state_whitespace
                  && opts->ignore_space
                     == svn_diff_file_ignore_space_change)
                /*### If we can postpone insertion of the space
                  until the next non-whitespace character,
                  we have a potential of reducing the number of copies:
                  If this space is followed by more spaces,
                  this will cause a block-copy.
                  If the next non-space block is considered normalized
                  *and* preceded by a space, we can take advantage of that. */
                /* Note, the above optimization applies to 90% of the source
                   lines in our own code, since it (generally) doesn't use
                   more than one space per blank section, except for the
                   beginning of a line. */
                INCLUDE_AS(' ');
              else
                SKIP;
              state = svn_diff__normalize_state_whitespace;
            }
          else
            {
              /* Non-whitespace character, or whitespace character in
                 svn_diff_file_ignore_space_none mode. */
              INCLUDE;
              state = svn_diff__normalize_state_normal;
            }
        }
    }

  /* If we're not in whitespace, flush the last chunk of data.
   * Note that this will work correctly when this is the last chunk of the
   * file:
   * * If there is an eol, it will either have been output when we entered
   *   the state_cr, or it will be output now.
   * * If there is no eol and we're not in whitespace, then we just output
   *   everything below.
   * * If there's no eol and we are in whitespace, we want to ignore
   *   whitespace unconditionally. */

  if (*tgt == tgt_newend)
    {
      /* we haven't copied any data in to *tgt and our chunk consists
         only of one block of (already normalized) data.
         Just return the block. */
      *tgt = (char *)start;
      *lengthp = include_len;
    }
  else
    {
      COPY_INCLUDED_SECTION;
      *lengthp = tgt_newend - *tgt;
    }

  *statep = state;

#undef SKIP
#undef INCLUDE
#undef INCLUDE_AS
#undef INSERT
#undef COPY_INCLUDED_SECTION
}

svn_error_t *
svn_diff__unified_append_no_newline_msg(svn_stringbuf_t *stringbuf,
                                        const char *header_encoding,
                                        apr_pool_t *scratch_pool)
{
  const char *out_str;

  SVN_ERR(svn_utf_cstring_from_utf8_ex2(
            &out_str,
            APR_EOL_STR
            SVN_DIFF__NO_NEWLINE_AT_END_OF_FILE APR_EOL_STR,
            header_encoding, scratch_pool));
  svn_stringbuf_appendcstr(stringbuf, out_str);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff__unified_write_hunk_header(svn_stream_t *output_stream,
                                    const char *header_encoding,
                                    const char *hunk_delimiter,
                                    apr_off_t old_start,
                                    apr_off_t old_length,
                                    apr_off_t new_start,
                                    apr_off_t new_length,
                                    const char *hunk_extra_context,
                                    apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_stream_printf_from_utf8(output_stream, header_encoding,
                                      scratch_pool,
                                      "%s -%" APR_OFF_T_FMT,
                                      hunk_delimiter, old_start));
  /* If the hunk length is 1, suppress the number of lines in the hunk
   * (it is 1 implicitly) */
  if (old_length != 1)
    {
      SVN_ERR(svn_stream_printf_from_utf8(output_stream, header_encoding,
                                          scratch_pool,
                                          ",%" APR_OFF_T_FMT, old_length));
    }

  SVN_ERR(svn_stream_printf_from_utf8(output_stream, header_encoding,
                                      scratch_pool,
                                      " +%" APR_OFF_T_FMT, new_start));
  if (new_length != 1)
    {
      SVN_ERR(svn_stream_printf_from_utf8(output_stream, header_encoding,
                                          scratch_pool,
                                          ",%" APR_OFF_T_FMT, new_length));
    }

  if (hunk_extra_context == NULL)
      hunk_extra_context = "";
  SVN_ERR(svn_stream_printf_from_utf8(output_stream, header_encoding,
                                      scratch_pool,
                                      " %s%s%s" APR_EOL_STR,
                                      hunk_delimiter,
                                      hunk_extra_context[0] ? " " : "",
                                      hunk_extra_context));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff__unidiff_write_header(svn_stream_t *output_stream,
                               const char *header_encoding,
                               const char *old_header,
                               const char *new_header,
                               apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_stream_printf_from_utf8(output_stream, header_encoding,
                                      scratch_pool,
                                      "--- %s" APR_EOL_STR
                                      "+++ %s" APR_EOL_STR,
                                      old_header,
                                      new_header));
  return SVN_NO_ERROR;
}

/* A helper function for display_prop_diffs.  Output the differences between
   the mergeinfo stored in ORIG_MERGEINFO_VAL and NEW_MERGEINFO_VAL in a
   human-readable form to OUTSTREAM, using ENCODING.  Use POOL for temporary
   allocations. */
static svn_error_t *
display_mergeinfo_diff(const char *old_mergeinfo_val,
                       const char *new_mergeinfo_val,
                       const char *encoding,
                       svn_stream_t *outstream,
                       apr_pool_t *pool)
{
  apr_hash_t *old_mergeinfo_hash, *new_mergeinfo_hash, *added, *deleted;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_hash_index_t *hi;

  if (old_mergeinfo_val)
    SVN_ERR(svn_mergeinfo_parse(&old_mergeinfo_hash, old_mergeinfo_val, pool));
  else
    old_mergeinfo_hash = NULL;

  if (new_mergeinfo_val)
    SVN_ERR(svn_mergeinfo_parse(&new_mergeinfo_hash, new_mergeinfo_val, pool));
  else
    new_mergeinfo_hash = NULL;

  SVN_ERR(svn_mergeinfo_diff2(&deleted, &added, old_mergeinfo_hash,
                              new_mergeinfo_hash,
                              TRUE, pool, pool));

  /* Print a hint for 'svn patch' or smilar tools, indicating the
   * number of reverse-merges and forward-merges. */
  SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, pool,
                                      "## -0,%u +0,%u ##%s",
                                      apr_hash_count(deleted),
                                      apr_hash_count(added),
                                      APR_EOL_STR));

  for (hi = apr_hash_first(pool, deleted);
       hi; hi = apr_hash_next(hi))
    {
      const char *from_path = apr_hash_this_key(hi);
      svn_rangelist_t *merge_revarray = apr_hash_this_val(hi);
      svn_string_t *merge_revstr;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_rangelist_to_string(&merge_revstr, merge_revarray,
                                      iterpool));

      SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, iterpool,
                                          _("   Reverse-merged %s:r%s%s"),
                                          from_path, merge_revstr->data,
                                          APR_EOL_STR));
    }

  for (hi = apr_hash_first(pool, added);
       hi; hi = apr_hash_next(hi))
    {
      const char *from_path = apr_hash_this_key(hi);
      svn_rangelist_t *merge_revarray = apr_hash_this_val(hi);
      svn_string_t *merge_revstr;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_rangelist_to_string(&merge_revstr, merge_revarray,
                                      iterpool));

      SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, iterpool,
                                          _("   Merged %s:r%s%s"),
                                          from_path, merge_revstr->data,
                                          APR_EOL_STR));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* svn_sort__array callback handling svn_prop_t by name */
static int
propchange_sort(const void *k1, const void *k2)
{
  const svn_prop_t *propchange1 = k1;
  const svn_prop_t *propchange2 = k2;

  return strcmp(propchange1->name, propchange2->name);
}

svn_error_t *
svn_diff__display_prop_diffs(svn_stream_t *outstream,
                             const char *encoding,
                             const apr_array_header_t *propchanges,
                             apr_hash_t *original_props,
                             svn_boolean_t pretty_print_mergeinfo,
                             int context_size,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             apr_pool_t *scratch_pool)
{
  apr_pool_t *pool = scratch_pool;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_array_header_t *changes = apr_array_copy(scratch_pool, propchanges);
  int i;

  svn_sort__array(changes, propchange_sort);

  for (i = 0; i < changes->nelts; i++)
    {
      const char *action;
      const svn_string_t *original_value;
      const svn_prop_t *propchange
        = &APR_ARRAY_IDX(changes, i, svn_prop_t);

      if (original_props)
        original_value = svn_hash_gets(original_props, propchange->name);
      else
        original_value = NULL;

      /* If the property doesn't exist on either side, or if it exists
         with the same value, skip it.  This can happen if the client is
         hitting an old mod_dav_svn server that doesn't understand the
         "send-all" REPORT style. */
      if ((! (original_value || propchange->value))
          || (original_value && propchange->value
              && svn_string_compare(original_value, propchange->value)))
        continue;

      svn_pool_clear(iterpool);

      if (! original_value)
        action = "Added";
      else if (! propchange->value)
        action = "Deleted";
      else
        action = "Modified";
      SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, iterpool,
                                          "%s: %s%s", action,
                                          propchange->name, APR_EOL_STR));

      if (pretty_print_mergeinfo
          && strcmp(propchange->name, SVN_PROP_MERGEINFO) == 0)
        {
          const char *orig = original_value ? original_value->data : NULL;
          const char *val = propchange->value ? propchange->value->data : NULL;
          svn_error_t *err = display_mergeinfo_diff(orig, val, encoding,
                                                    outstream, iterpool);

          /* Issue #3896: If we can't pretty-print mergeinfo differences
             because invalid mergeinfo is present, then don't let the diff
             fail, just print the diff as any other property. */
          if (err && err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
            {
              svn_error_clear(err);
            }
          else
            {
              SVN_ERR(err);
              continue;
            }
        }

      {
        svn_diff_t *diff;
        svn_diff_file_options_t options = { 0 };
        const svn_string_t *orig
          = original_value ? original_value
                           : svn_string_create_empty(iterpool);
        const svn_string_t *val
          = propchange->value ? propchange->value
                              : svn_string_create_empty(iterpool);

        SVN_ERR(svn_diff_mem_string_diff(&diff, orig, val, &options,
                                         iterpool));

        /* UNIX patch will try to apply a diff even if the diff header
         * is missing. It tries to be helpful by asking the user for a
         * target filename when it can't determine the target filename
         * from the diff header. But there usually are no files which
         * UNIX patch could apply the property diff to, so we use "##"
         * instead of "@@" as the default hunk delimiter for property diffs.
         * We also suppress the diff header. */
        SVN_ERR(svn_diff_mem_string_output_unified3(
                  outstream, diff, FALSE /* no header */, "##", NULL, NULL,
                  encoding, orig, val, context_size,
                  cancel_func, cancel_baton, iterpool));
      }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Return the library version number. */
const svn_version_t *
svn_diff_version(void)
{
  SVN_VERSION_BODY;
}
