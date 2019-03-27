/*
 * log-cmd.c -- Display log messages
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

#include <apr_fnmatch.h>

#include "svn_client.h"
#include "svn_compat.h"
#include "svn_dirent_uri.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_sorts.h"
#include "svn_xml.h"
#include "svn_time.h"
#include "svn_cmdline.h"
#include "svn_props.h"
#include "svn_pools.h"

#include "private/svn_cmdline_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_utf_private.h"

#include "cl.h"
#include "cl-log.h"

#include "svn_private_config.h"


/*** Code. ***/



/* Display a diff of the subtree TARGET_PATH_OR_URL@TARGET_PEG_REVISION as
 * it changed in the revision that LOG_ENTRY describes.
 *
 * Restrict the diff to depth DEPTH.  Pass DIFF_EXTENSIONS along to the diff
 * subroutine.
 *
 * Write the diff to OUTSTREAM and write any stderr output to ERRSTREAM.
 * ### How is exit code handled? 0 and 1 -> SVN_NO_ERROR, else an svn error?
 * ### Should we get rid of ERRSTREAM and use svn_error_t instead?
 */
static svn_error_t *
display_diff(const svn_log_entry_t *log_entry,
             const char *target_path_or_url,
             const svn_opt_revision_t *target_peg_revision,
             svn_depth_t depth,
             const char *diff_extensions,
             svn_stream_t *outstream,
             svn_stream_t *errstream,
             svn_client_ctx_t *ctx,
             apr_pool_t *pool)
{
  apr_array_header_t *diff_options;
  svn_opt_revision_t start_revision;
  svn_opt_revision_t end_revision;

  /* Fall back to "" to get options initialized either way. */
  if (diff_extensions)
    diff_options = svn_cstring_split(diff_extensions, " \t\n\r",
                                     TRUE, pool);
  else
    diff_options = NULL;

  start_revision.kind = svn_opt_revision_number;
  start_revision.value.number = log_entry->revision - 1;
  end_revision.kind = svn_opt_revision_number;
  end_revision.value.number = log_entry->revision;

  SVN_ERR(svn_stream_puts(outstream, "\n"));
  SVN_ERR(svn_client_diff_peg6(diff_options,
                               target_path_or_url,
                               target_peg_revision,
                               &start_revision, &end_revision,
                               NULL,
                               depth,
                               FALSE /* ignore ancestry */,
                               FALSE /* no diff added */,
                               TRUE  /* no diff deleted */,
                               FALSE /* show copies as adds */,
                               FALSE /* ignore content type */,
                               FALSE /* ignore prop diff */,
                               FALSE /* properties only */,
                               FALSE /* use git diff format */,
                               svn_cmdline_output_encoding(pool),
                               outstream,
                               errstream,
                               NULL,
                               ctx, pool));
  SVN_ERR(svn_stream_puts(outstream, _("\n")));
  return SVN_NO_ERROR;
}

/* Return TRUE if STR matches PATTERN. Else, return FALSE. Assumes that
 * PATTERN is a UTF-8 string prepared for case- and accent-insensitive
 * comparison via svn_utf__xfrm(). */
static svn_boolean_t
match(const char *pattern, const char *str, svn_membuf_t *buf)
{
  svn_error_t *err;

  err = svn_utf__xfrm(&str, str, strlen(str), TRUE, TRUE, buf);
  if (err)
    {
      /* Can't match invalid data. */
      svn_error_clear(err);
      return FALSE;
    }

  return apr_fnmatch(pattern, str, 0) == APR_SUCCESS;
}

/* Return TRUE if SEARCH_PATTERN matches the AUTHOR, DATE, LOG_MESSAGE,
 * or a path in the set of keys of the CHANGED_PATHS hash. Else, return FALSE.
 * Any of AUTHOR, DATE, LOG_MESSAGE, and CHANGED_PATHS may be NULL. */
static svn_boolean_t
match_search_pattern(const char *search_pattern,
                     const char *author,
                     const char *date,
                     const char *log_message,
                     apr_hash_t *changed_paths,
                     svn_membuf_t *buf,
                     apr_pool_t *pool)
{
  /* Match any substring containing the pattern, like UNIX 'grep' does. */
  const char *pattern = apr_psprintf(pool, "*%s*", search_pattern);

  /* Does the author match the search pattern? */
  if (author && match(pattern, author, buf))
    return TRUE;

  /* Does the date the search pattern? */
  if (date && match(pattern, date, buf))
    return TRUE;

  /* Does the log message the search pattern? */
  if (log_message && match(pattern, log_message, buf))
    return TRUE;

  if (changed_paths)
    {
      apr_hash_index_t *hi;

      /* Does a changed path match the search pattern? */
      for (hi = apr_hash_first(pool, changed_paths);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *path = apr_hash_this_key(hi);
          svn_log_changed_path2_t *log_item;

          if (match(pattern, path, buf))
            return TRUE;

          /* Match copy-from paths, too. */
          log_item = apr_hash_this_val(hi);
          if (log_item->copyfrom_path
              && SVN_IS_VALID_REVNUM(log_item->copyfrom_rev)
              && match(pattern, log_item->copyfrom_path, buf))
            return TRUE;
        }
    }

  return FALSE;
}

/* Match all search patterns in SEARCH_PATTERNS against AUTHOR, DATE, MESSAGE,
 * and CHANGED_PATHS. Return TRUE if any pattern matches, else FALSE.
 * BUF and SCRATCH_POOL are used for temporary allocations. */
static svn_boolean_t
match_search_patterns(apr_array_header_t *search_patterns,
                      const char *author,
                      const char *date,
                      const char *message,
                      apr_hash_t *changed_paths,
                      svn_membuf_t *buf,
                      apr_pool_t *scratch_pool)
{
  int i;
  svn_boolean_t match = FALSE;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  for (i = 0; i < search_patterns->nelts; i++)
    {
      apr_array_header_t *pattern_group;
      int j;

      pattern_group = APR_ARRAY_IDX(search_patterns, i, apr_array_header_t *);

      /* All patterns within the group must match. */
      for (j = 0; j < pattern_group->nelts; j++)
        {
          const char *pattern;

          svn_pool_clear(iterpool);

          pattern = APR_ARRAY_IDX(pattern_group, j, const char *);
          match = match_search_pattern(pattern, author, date, message,
                                       changed_paths, buf, iterpool);
          if (!match)
            break;
        }

      match = (match && j == pattern_group->nelts);
      if (match)
        break;
    }
  svn_pool_destroy(iterpool);

  return match;
}

/* Implement `svn_log_entry_receiver_t', printing the logs in
 * a human-readable and machine-parseable format.
 *
 * BATON is of type `svn_cl__log_receiver_baton'.
 *
 * First, print a header line.  Then if CHANGED_PATHS is non-null,
 * print all affected paths in a list headed "Changed paths:\n",
 * immediately following the header line.  Then print a newline
 * followed by the message body, unless BATON->omit_log_message is true.
 *
 * Here are some examples of the output:
 *
 * $ svn log -r1847:1846
 * ------------------------------------------------------------------------
 * rev 1847:  cmpilato | Wed 1 May 2002 15:44:26 | 7 lines
 *
 * Fix for Issue #694.
 *
 * * subversion/libsvn_repos/delta.c
 *   (delta_files): Rework the logic in this function to only call
 * send_text_deltas if there are deltas to send, and within that case,
 * only use a real delta stream if the caller wants real text deltas.
 *
 * ------------------------------------------------------------------------
 * rev 1846:  whoever | Wed 1 May 2002 15:23:41 | 1 line
 *
 * imagine an example log message here
 * ------------------------------------------------------------------------
 *
 * Or:
 *
 * $ svn log -r1847:1846 -v
 * ------------------------------------------------------------------------
 * rev 1847:  cmpilato | Wed 1 May 2002 15:44:26 | 7 lines
 * Changed paths:
 *    M /trunk/subversion/libsvn_repos/delta.c
 *
 * Fix for Issue #694.
 *
 * * subversion/libsvn_repos/delta.c
 *   (delta_files): Rework the logic in this function to only call
 * send_text_deltas if there are deltas to send, and within that case,
 * only use a real delta stream if the caller wants real text deltas.
 *
 * ------------------------------------------------------------------------
 * rev 1846:  whoever | Wed 1 May 2002 15:23:41 | 1 line
 * Changed paths:
 *    M /trunk/notes/fs_dumprestore.txt
 *    M /trunk/subversion/libsvn_repos/dump.c
 *
 * imagine an example log message here
 * ------------------------------------------------------------------------
 *
 * Or:
 *
 * $ svn log -r1847:1846 -q
 * ------------------------------------------------------------------------
 * rev 1847:  cmpilato | Wed 1 May 2002 15:44:26
 * ------------------------------------------------------------------------
 * rev 1846:  whoever | Wed 1 May 2002 15:23:41
 * ------------------------------------------------------------------------
 *
 * Or:
 *
 * $ svn log -r1847:1846 -qv
 * ------------------------------------------------------------------------
 * rev 1847:  cmpilato | Wed 1 May 2002 15:44:26
 * Changed paths:
 *    M /trunk/subversion/libsvn_repos/delta.c
 * ------------------------------------------------------------------------
 * rev 1846:  whoever | Wed 1 May 2002 15:23:41
 * Changed paths:
 *    M /trunk/notes/fs_dumprestore.txt
 *    M /trunk/subversion/libsvn_repos/dump.c
 * ------------------------------------------------------------------------
 *
 */
svn_error_t *
svn_cl__log_entry_receiver(void *baton,
                           svn_log_entry_t *log_entry,
                           apr_pool_t *pool)
{
  svn_cl__log_receiver_baton *lb = baton;
  const char *author;
  const char *date;
  const char *message;

  if (lb->ctx->cancel_func)
    SVN_ERR(lb->ctx->cancel_func(lb->ctx->cancel_baton));

  svn_compat_log_revprops_out(&author, &date, &message, log_entry->revprops);

  if (log_entry->revision == 0 && message == NULL)
    return SVN_NO_ERROR;

  if (! SVN_IS_VALID_REVNUM(log_entry->revision))
    {
      if (lb->merge_stack)
        apr_array_pop(lb->merge_stack);

      return SVN_NO_ERROR;
    }

  /* ### See http://subversion.tigris.org/issues/show_bug.cgi?id=807
     for more on the fallback fuzzy conversions below. */

  if (author == NULL)
    author = _("(no author)");

  if (date && date[0])
    /* Convert date to a format for humans. */
    SVN_ERR(svn_cl__time_cstring_to_human_cstring(&date, date, pool));
  else
    date = _("(no date)");

  if (! lb->omit_log_message && message == NULL)
    message = "";

  if (lb->search_patterns &&
      ! match_search_patterns(lb->search_patterns, author, date, message,
                              log_entry->changed_paths2, &lb->buffer, pool))
    {
      if (log_entry->has_children)
        {
          if (! lb->merge_stack)
            lb->merge_stack = apr_array_make(lb->pool, 1, sizeof(svn_revnum_t));

          APR_ARRAY_PUSH(lb->merge_stack, svn_revnum_t) = log_entry->revision;
        }

      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_cmdline_printf(pool,
                             SVN_CL__LOG_SEP_STRING "r%ld | %s | %s",
                             log_entry->revision, author, date));

  if (message != NULL)
    {
      /* Number of lines in the msg. */
      int lines = svn_cstring_count_newlines(message) + 1;

      SVN_ERR(svn_cmdline_printf(pool,
                                 Q_(" | %d line", " | %d lines", lines),
                                 lines));
    }

  SVN_ERR(svn_cmdline_printf(pool, "\n"));

  if (log_entry->changed_paths2)
    {
      apr_array_header_t *sorted_paths;
      int i;
      apr_pool_t *iterpool;

      /* Get an array of sorted hash keys. */
      sorted_paths = svn_sort__hash(log_entry->changed_paths2,
                                    svn_sort_compare_items_as_paths, pool);

      SVN_ERR(svn_cmdline_printf(pool,
                                 _("Changed paths:\n")));
      iterpool = svn_pool_create(pool);
      for (i = 0; i < sorted_paths->nelts; i++)
        {
          svn_sort__item_t *item = &(APR_ARRAY_IDX(sorted_paths, i,
                                                   svn_sort__item_t));
          const char *path = item->key;
          svn_log_changed_path2_t *log_item = item->value;
          const char *copy_data = "";

          svn_pool_clear(iterpool);

          if (lb->ctx->cancel_func)
            SVN_ERR(lb->ctx->cancel_func(lb->ctx->cancel_baton));

          if (log_item->copyfrom_path
              && SVN_IS_VALID_REVNUM(log_item->copyfrom_rev))
            {
              copy_data
                = apr_psprintf(iterpool,
                               _(" (from %s:%ld)"),
                               log_item->copyfrom_path,
                               log_item->copyfrom_rev);
            }
          SVN_ERR(svn_cmdline_printf(iterpool, "   %c %s%s\n",
                                     log_item->action, path,
                                     copy_data));
        }
      svn_pool_destroy(iterpool);
    }

  if (lb->merge_stack && lb->merge_stack->nelts > 0)
    {
      int i;
      apr_pool_t *iterpool;

      /* Print the result of merge line */
      if (log_entry->subtractive_merge)
        SVN_ERR(svn_cmdline_printf(pool, _("Reverse merged via:")));
      else
        SVN_ERR(svn_cmdline_printf(pool, _("Merged via:")));
      iterpool = svn_pool_create(pool);
      for (i = 0; i < lb->merge_stack->nelts; i++)
        {
          svn_revnum_t rev = APR_ARRAY_IDX(lb->merge_stack, i, svn_revnum_t);

          svn_pool_clear(iterpool);
          SVN_ERR(svn_cmdline_printf(iterpool, " r%ld%c", rev,
                                     i == lb->merge_stack->nelts - 1 ?
                                                                  '\n' : ','));
        }
      svn_pool_destroy(iterpool);
    }

  if (message != NULL)
    {
      /* A blank line always precedes the log message. */
      SVN_ERR(svn_cmdline_printf(pool, "\n%s\n", message));
    }

  SVN_ERR(svn_cmdline_fflush(stdout));
  SVN_ERR(svn_cmdline_fflush(stderr));

  /* Print a diff if requested. */
  if (lb->show_diff)
    {
      svn_stream_t *outstream;
      svn_stream_t *errstream;

      SVN_ERR(svn_stream_for_stdout(&outstream, pool));
      SVN_ERR(svn_stream_for_stderr(&errstream, pool));

      SVN_ERR(display_diff(log_entry,
                           lb->target_path_or_url, &lb->target_peg_revision,
                           lb->depth, lb->diff_extensions,
                           outstream, errstream,
                           lb->ctx, pool));

      SVN_ERR(svn_stream_close(outstream));
      SVN_ERR(svn_stream_close(errstream));
    }

  if (log_entry->has_children)
    {
      if (! lb->merge_stack)
        lb->merge_stack = apr_array_make(lb->pool, 1, sizeof(svn_revnum_t));

      APR_ARRAY_PUSH(lb->merge_stack, svn_revnum_t) = log_entry->revision;
    }

  return SVN_NO_ERROR;
}


/* This implements `svn_log_entry_receiver_t', printing the logs in XML.
 *
 * BATON is of type `svn_cl__log_receiver_baton'.
 *
 * Here is an example of the output; note that the "<log>" and
 * "</log>" tags are not emitted by this function:
 *
 * $ svn log --xml -r 1648:1649
 * <log>
 * <logentry
 *    revision="1648">
 * <author>david</author>
 * <date>2002-04-06T16:34:51.428043Z</date>
 * <msg> * packages/rpm/subversion.spec : Now requires apache 2.0.36.
 * </msg>
 * </logentry>
 * <logentry
 *    revision="1649">
 * <author>cmpilato</author>
 * <date>2002-04-06T17:01:28.185136Z</date>
 * <msg>Fix error handling when the $EDITOR is needed but unavailable.  Ah
 * ... now that&apos;s *much* nicer.
 *
 * * subversion/clients/cmdline/util.c
 *   (svn_cl__edit_externally): Clean up the &quot;no external editor&quot;
 *   error message.
 *   (svn_cl__get_log_message): Wrap &quot;no external editor&quot;
 *   errors with helpful hints about the -m and -F options.
 *
 * * subversion/libsvn_client/commit.c
 *   (svn_client_commit): Actually capture and propagate &quot;no external
 *   editor&quot; errors.</msg>
 * </logentry>
 * </log>
 *
 */
svn_error_t *
svn_cl__log_entry_receiver_xml(void *baton,
                               svn_log_entry_t *log_entry,
                               apr_pool_t *pool)
{
  svn_cl__log_receiver_baton *lb = baton;
  /* Collate whole log message into sb before printing. */
  svn_stringbuf_t *sb = svn_stringbuf_create_empty(pool);
  char *revstr;
  const char *author;
  const char *date;
  const char *message;

  if (lb->ctx->cancel_func)
    SVN_ERR(lb->ctx->cancel_func(lb->ctx->cancel_baton));

  svn_compat_log_revprops_out(&author, &date, &message, log_entry->revprops);

  if (log_entry->revision == 0 && message == NULL)
    return SVN_NO_ERROR;

  if (! SVN_IS_VALID_REVNUM(log_entry->revision))
    {
      svn_xml_make_close_tag(&sb, pool, "logentry");
      SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));
      if (lb->merge_stack)
        apr_array_pop(lb->merge_stack);

      return SVN_NO_ERROR;
    }

  /* Match search pattern before XML-escaping. */
  if (lb->search_patterns &&
      ! match_search_patterns(lb->search_patterns, author, date, message,
                              log_entry->changed_paths2, &lb->buffer, pool))
    {
      if (log_entry->has_children)
        {
          if (! lb->merge_stack)
            lb->merge_stack = apr_array_make(lb->pool, 1, sizeof(svn_revnum_t));

          APR_ARRAY_PUSH(lb->merge_stack, svn_revnum_t) = log_entry->revision;
        }

      return SVN_NO_ERROR;
    }

  if (author)
    author = svn_xml_fuzzy_escape(author, pool);
  if (date)
    date = svn_xml_fuzzy_escape(date, pool);
  if (message)
    message = svn_xml_fuzzy_escape(message, pool);

  revstr = apr_psprintf(pool, "%ld", log_entry->revision);
  /* <logentry revision="xxx"> */
  if (lb->merge_stack && lb->merge_stack->nelts > 0)
    {
      svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "logentry",
                            "revision", revstr, "reverse-merge",
                            log_entry->subtractive_merge ? "true" : "false",
                            SVN_VA_NULL);

    }
  else
    {
        svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "logentry",
                              "revision", revstr, SVN_VA_NULL);
    }

  /* <author>xxx</author> */
  svn_cl__xml_tagged_cdata(&sb, pool, "author", author);

  /* Print the full, uncut, date.  This is machine output. */
  /* According to the docs for svn_log_entry_receiver_t, either
     NULL or the empty string represents no date.  Avoid outputting an
     empty date element. */
  if (date && date[0] == '\0')
    date = NULL;
  /* <date>xxx</date> */
  svn_cl__xml_tagged_cdata(&sb, pool, "date", date);

  if (log_entry->changed_paths2)
    {
      apr_array_header_t *sorted_paths;
      int i;

      /* <paths> */
      svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "paths",
                            SVN_VA_NULL);

      /* Get an array of sorted hash keys. */
      sorted_paths = svn_sort__hash(log_entry->changed_paths2,
                                    svn_sort_compare_items_as_paths, pool);

      for (i = 0; i < sorted_paths->nelts; i++)
        {
          svn_sort__item_t *item = &(APR_ARRAY_IDX(sorted_paths, i,
                                                   svn_sort__item_t));
          const char *path = item->key;
          svn_log_changed_path2_t *log_item = item->value;
          char action[2];

          action[0] = log_item->action;
          action[1] = '\0';
          if (log_item->copyfrom_path
              && SVN_IS_VALID_REVNUM(log_item->copyfrom_rev))
            {
              /* <path action="X" copyfrom-path="xxx" copyfrom-rev="xxx"> */
              revstr = apr_psprintf(pool, "%ld",
                                    log_item->copyfrom_rev);
              svn_xml_make_open_tag(&sb, pool, svn_xml_protect_pcdata, "path",
                                    "action", action,
                                    "copyfrom-path", log_item->copyfrom_path,
                                    "copyfrom-rev", revstr,
                                    "kind", svn_cl__node_kind_str_xml(
                                                     log_item->node_kind),
                                    "text-mods", svn_tristate__to_word(
                                                     log_item->text_modified),
                                    "prop-mods", svn_tristate__to_word(
                                                     log_item->props_modified),
                                    SVN_VA_NULL);
            }
          else
            {
              /* <path action="X"> */
              svn_xml_make_open_tag(&sb, pool, svn_xml_protect_pcdata, "path",
                                    "action", action,
                                    "kind", svn_cl__node_kind_str_xml(
                                                     log_item->node_kind),
                                    "text-mods", svn_tristate__to_word(
                                                     log_item->text_modified),
                                    "prop-mods", svn_tristate__to_word(
                                                     log_item->props_modified),
                                    SVN_VA_NULL);
            }
          /* xxx</path> */
          svn_xml_escape_cdata_cstring(&sb, path, pool);
          svn_xml_make_close_tag(&sb, pool, "path");
        }

      /* </paths> */
      svn_xml_make_close_tag(&sb, pool, "paths");
    }

  if (message != NULL)
    {
      /* <msg>xxx</msg> */
      svn_cl__xml_tagged_cdata(&sb, pool, "msg", message);
    }

  svn_compat_log_revprops_clear(log_entry->revprops);
  if (log_entry->revprops && apr_hash_count(log_entry->revprops) > 0)
    {
      svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "revprops", SVN_VA_NULL);
      SVN_ERR(svn_cmdline__print_xml_prop_hash(&sb, log_entry->revprops,
                                               FALSE, /* name_only */
                                               FALSE, pool));
      svn_xml_make_close_tag(&sb, pool, "revprops");
    }

  if (log_entry->has_children)
    {
      if (! lb->merge_stack)
        lb->merge_stack = apr_array_make(lb->pool, 1, sizeof(svn_revnum_t));

      APR_ARRAY_PUSH(lb->merge_stack, svn_revnum_t) = log_entry->revision;
    }
  else
    svn_xml_make_close_tag(&sb, pool, "logentry");

  return svn_cl__error_checked_fputs(sb->data, stdout);
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__log(apr_getopt_t *os,
            void *baton,
            apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  svn_cl__log_receiver_baton lb;
  const char *target;
  int i;
  apr_array_header_t *revprops;

  if (!opt_state->xml)
    {
      if (opt_state->all_revprops)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("'with-all-revprops' option only valid in"
                                  " XML mode"));
      if (opt_state->no_revprops)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("'with-no-revprops' option only valid in"
                                  " XML mode"));
      if (opt_state->revprop_table != NULL)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("'with-revprop' option only valid in"
                                  " XML mode"));
    }
  else
    {
      if (opt_state->show_diff)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("'diff' option is not supported in "
                                  "XML mode"));
    }

  if (opt_state->quiet && opt_state->show_diff)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("'quiet' and 'diff' options are "
                              "mutually exclusive"));
  if (opt_state->diff.diff_cmd && (! opt_state->show_diff))
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("'diff-cmd' option requires 'diff' "
                              "option"));
  if (opt_state->diff.internal_diff && (! opt_state->show_diff))
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("'internal-diff' option requires "
                              "'diff' option"));
  if (opt_state->extensions && (! opt_state->show_diff))
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("'extensions' option requires 'diff' "
                              "option"));

  if (opt_state->depth != svn_depth_unknown && (! opt_state->show_diff))
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("'depth' option requires 'diff' option"));

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* Add "." if user passed 0 arguments */
  svn_opt_push_implicit_dot_target(targets, pool);

  /* Determine if they really want a two-revision range. */
  if (opt_state->used_change_arg)
    {
      if (opt_state->used_revision_arg && opt_state->revision_ranges->nelts > 1)
        {
          return svn_error_create
            (SVN_ERR_CLIENT_BAD_REVISION, NULL,
             _("-c and -r are mutually exclusive"));
        }
      for (i = 0; i < opt_state->revision_ranges->nelts; i++)
        {
          svn_opt_revision_range_t *range;
          range = APR_ARRAY_IDX(opt_state->revision_ranges, i,
                                svn_opt_revision_range_t *);
          if (range->start.value.number < range->end.value.number)
            range->start.value.number++;
          else
            range->end.value.number++;
        }
    }

  /* Parse the first target into path-or-url and peg revision. */
  target = APR_ARRAY_IDX(targets, 0, const char *);
  SVN_ERR(svn_opt_parse_path(&lb.target_peg_revision, &lb.target_path_or_url,
                             target, pool));
  if (lb.target_peg_revision.kind == svn_opt_revision_unspecified)
    lb.target_peg_revision.kind = (svn_path_is_url(target)
                                     ? svn_opt_revision_head
                                     : svn_opt_revision_working);
  APR_ARRAY_IDX(targets, 0, const char *) = lb.target_path_or_url;

  if (svn_path_is_url(target))
    {
      for (i = 1; i < targets->nelts; i++)
        {
          target = APR_ARRAY_IDX(targets, i, const char *);

          if (svn_path_is_url(target) || target[0] == '/')
            return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                     _("Only relative paths can be specified"
                                       " after a URL for 'svn log', "
                                       "but '%s' is not a relative path"),
                                     target);
        }
    }

  lb.ctx = ctx;
  lb.omit_log_message = opt_state->quiet;
  lb.show_diff = opt_state->show_diff;
  lb.depth = opt_state->depth == svn_depth_unknown ? svn_depth_infinity
                                                   : opt_state->depth;
  lb.diff_extensions = opt_state->extensions;
  lb.merge_stack = NULL;
  lb.search_patterns = opt_state->search_patterns;
  svn_membuf__create(&lb.buffer, 0, pool);
  lb.pool = pool;

  if (opt_state->xml)
    {
      /* If output is not incremental, output the XML header and wrap
         everything in a top-level element. This makes the output in
         its entirety a well-formed XML document. */
      if (! opt_state->incremental)
        SVN_ERR(svn_cl__xml_print_header("log", pool));

      if (opt_state->all_revprops)
        revprops = NULL;
      else if(opt_state->no_revprops)
        {
          revprops = apr_array_make(pool, 0, sizeof(char *));
        }
      else if (opt_state->revprop_table != NULL)
        {
          apr_hash_index_t *hi;
          revprops = apr_array_make(pool,
                                    apr_hash_count(opt_state->revprop_table),
                                    sizeof(char *));
          for (hi = apr_hash_first(pool, opt_state->revprop_table);
               hi != NULL;
               hi = apr_hash_next(hi))
            {
              const char *property = apr_hash_this_key(hi);
              svn_string_t *value = apr_hash_this_val(hi);

              if (value && value->data[0] != '\0')
                return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                         _("cannot assign with 'with-revprop'"
                                           " option (drop the '=')"));
              APR_ARRAY_PUSH(revprops, const char *) = property;
            }
        }
      else
        {
          revprops = apr_array_make(pool, 3, sizeof(char *));
          APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_AUTHOR;
          APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_DATE;
          if (!opt_state->quiet)
            APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_LOG;
        }
      SVN_ERR(svn_client_log5(targets,
                              &lb.target_peg_revision,
                              opt_state->revision_ranges,
                              opt_state->limit,
                              opt_state->verbose,
                              opt_state->stop_on_copy,
                              opt_state->use_merge_history,
                              revprops,
                              svn_cl__log_entry_receiver_xml,
                              &lb,
                              ctx,
                              pool));

      if (! opt_state->incremental)
        SVN_ERR(svn_cl__xml_print_footer("log", pool));
    }
  else  /* default output format */
    {
      revprops = apr_array_make(pool, 3, sizeof(char *));
      APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_AUTHOR;
      APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_DATE;
      if (!opt_state->quiet)
        APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_LOG;
      SVN_ERR(svn_client_log5(targets,
                              &lb.target_peg_revision,
                              opt_state->revision_ranges,
                              opt_state->limit,
                              opt_state->verbose,
                              opt_state->stop_on_copy,
                              opt_state->use_merge_history,
                              revprops,
                              svn_cl__log_entry_receiver,
                              &lb,
                              ctx,
                              pool));

      if (! opt_state->incremental)
        SVN_ERR(svn_cmdline_printf(pool, SVN_CL__LOG_SEP_STRING));
    }

  return SVN_NO_ERROR;
}
