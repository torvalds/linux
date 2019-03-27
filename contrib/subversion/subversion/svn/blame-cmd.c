/*
 * blame-cmd.c -- Display blame information
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


/*** Includes. ***/

#include "svn_client.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_cmdline.h"
#include "svn_sorts.h"
#include "svn_xml.h"
#include "svn_time.h"
#include "cl.h"

#include "svn_private_config.h"

typedef struct blame_baton_t
{
  svn_cl__opt_state_t *opt_state;
  svn_stream_t *out;
  svn_stringbuf_t *sbuf;

  int rev_maxlength;
} blame_baton_t;


/*** Code. ***/

/* This implements the svn_client_blame_receiver3_t interface, printing
   XML to stdout. */
static svn_error_t *
blame_receiver_xml(void *baton,
                   svn_revnum_t start_revnum,
                   svn_revnum_t end_revnum,
                   apr_int64_t line_no,
                   svn_revnum_t revision,
                   apr_hash_t *rev_props,
                   svn_revnum_t merged_revision,
                   apr_hash_t *merged_rev_props,
                   const char *merged_path,
                   const char *line,
                   svn_boolean_t local_change,
                   apr_pool_t *pool)
{
  blame_baton_t *bb = baton;
  svn_cl__opt_state_t *opt_state = bb->opt_state;
  svn_stringbuf_t *sb = bb->sbuf;

  /* "<entry ...>" */
  /* line_no is 0-based, but the rest of the world is probably Pascal
     programmers, so we make them happy and output 1-based line numbers. */
  svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "entry",
                        "line-number",
                        apr_psprintf(pool, "%" APR_INT64_T_FMT,
                                     line_no + 1),
                        SVN_VA_NULL);

  if (SVN_IS_VALID_REVNUM(revision))
    svn_cl__print_xml_commit(&sb, revision,
                             svn_prop_get_value(rev_props,
                                                SVN_PROP_REVISION_AUTHOR),
                             svn_prop_get_value(rev_props,
                                                SVN_PROP_REVISION_DATE),
                             pool);

  if (opt_state->use_merge_history && SVN_IS_VALID_REVNUM(merged_revision))
    {
      /* "<merged>" */
      svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "merged",
                            "path", merged_path, SVN_VA_NULL);

      svn_cl__print_xml_commit(&sb, merged_revision,
                             svn_prop_get_value(merged_rev_props,
                                                SVN_PROP_REVISION_AUTHOR),
                             svn_prop_get_value(merged_rev_props,
                                                SVN_PROP_REVISION_DATE),
                             pool);

      /* "</merged>" */
      svn_xml_make_close_tag(&sb, pool, "merged");

    }

  /* "</entry>" */
  svn_xml_make_close_tag(&sb, pool, "entry");

  SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));
  svn_stringbuf_setempty(sb);

  return SVN_NO_ERROR;
}


static svn_error_t *
print_line_info(svn_stream_t *out,
                svn_revnum_t revision,
                const char *author,
                const char *date,
                const char *path,
                svn_boolean_t verbose,
                int rev_maxlength,
                apr_pool_t *pool)
{
  const char *time_utf8;
  const char *time_stdout;
  const char *rev_str;

  rev_str = SVN_IS_VALID_REVNUM(revision)
    ? apr_psprintf(pool, "%*ld", rev_maxlength, revision)
    : apr_psprintf(pool, "%*s", rev_maxlength, "-");

  if (verbose)
    {
      if (date)
        {
          SVN_ERR(svn_cl__time_cstring_to_human_cstring(&time_utf8,
                                                        date, pool));
          SVN_ERR(svn_cmdline_cstring_from_utf8(&time_stdout, time_utf8,
                                                pool));
        }
      else
        {
          /* ### This is a 44 characters long string. It assumes the current
             format of svn_time_to_human_cstring and also 3 letter
             abbreviations for the month and weekday names.  Else, the
             line contents will be misaligned. */
          time_stdout = "                                           -";
        }

      SVN_ERR(svn_stream_printf(out, pool, "%s %10s %s ", rev_str,
                                author ? author : "         -",
                                time_stdout));

      if (path)
        SVN_ERR(svn_stream_printf(out, pool, "%-14s ", path));
    }
  else
    {
      return svn_stream_printf(out, pool, "%s %10.10s ", rev_str,
                               author ? author : "         -");
    }

  return SVN_NO_ERROR;
}

/* This implements the svn_client_blame_receiver3_t interface. */
static svn_error_t *
blame_receiver(void *baton,
               svn_revnum_t start_revnum,
               svn_revnum_t end_revnum,
               apr_int64_t line_no,
               svn_revnum_t revision,
               apr_hash_t *rev_props,
               svn_revnum_t merged_revision,
               apr_hash_t *merged_rev_props,
               const char *merged_path,
               const char *line,
               svn_boolean_t local_change,
               apr_pool_t *pool)
{
  blame_baton_t *bb = baton;
  svn_cl__opt_state_t *opt_state = bb->opt_state;
  svn_stream_t *out = bb->out;
  svn_boolean_t use_merged = FALSE;

  if (!bb->rev_maxlength)
    {
      svn_revnum_t max_revnum = MAX(start_revnum, end_revnum);
      /* The standard column width for the revision number is 6 characters.
         If the revision number can potentially be larger (i.e. if the end_revnum
          is larger than 1000000), we increase the column width as needed. */

      bb->rev_maxlength = 6;
      while (max_revnum >= 1000000)
        {
          bb->rev_maxlength++;
          max_revnum = max_revnum / 10;
        }
    }

  if (opt_state->use_merge_history)
    {
      /* Choose which revision to use.  If they aren't equal, prefer the
         earliest revision.  Since we do a forward blame, we want to the first
         revision which put the line in its current state, so we use the
         earliest revision.  If we ever switch to a backward blame algorithm,
         we may need to adjust this. */
      if (merged_revision < revision)
        {
          SVN_ERR(svn_stream_puts(out, "G "));
          use_merged = TRUE;
        }
      else
        SVN_ERR(svn_stream_puts(out, "  "));
    }

  if (use_merged)
    SVN_ERR(print_line_info(out, merged_revision,
                            svn_prop_get_value(merged_rev_props,
                                               SVN_PROP_REVISION_AUTHOR),
                            svn_prop_get_value(merged_rev_props,
                                               SVN_PROP_REVISION_DATE),
                            merged_path, opt_state->verbose,
                            bb->rev_maxlength,
                            pool));
  else
    SVN_ERR(print_line_info(out, revision,
                            svn_prop_get_value(rev_props,
                                               SVN_PROP_REVISION_AUTHOR),
                            svn_prop_get_value(rev_props,
                                               SVN_PROP_REVISION_DATE),
                            NULL, opt_state->verbose,
                            bb->rev_maxlength,
                            pool));

  return svn_stream_printf(out, pool, "%s%s", line, APR_EOL_STR);
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__blame(apr_getopt_t *os,
              void *baton,
              apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_pool_t *subpool;
  apr_array_header_t *targets;
  blame_baton_t bl;
  int i;
  svn_boolean_t end_revision_unspecified = FALSE;
  svn_diff_file_options_t *diff_options = svn_diff_file_options_create(pool);
  svn_boolean_t seen_nonexistent_target = FALSE;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* Blame needs a file on which to operate. */
  if (! targets->nelts)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
    {
      if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
        {
          /* In the case that -rX was specified, we actually want to set the
             range to be -r1:X. */

          opt_state->end_revision = opt_state->start_revision;
          opt_state->start_revision.kind = svn_opt_revision_number;
          opt_state->start_revision.value.number = 1;
        }
      else
        end_revision_unspecified = TRUE;
    }

  if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
    {
      opt_state->start_revision.kind = svn_opt_revision_number;
      opt_state->start_revision.value.number = 1;
    }

  /* The final conclusion from issue #2431 is that blame info
     is client output (unlike 'svn cat' which plainly cats the file),
     so the EOL style should be the platform local one.
  */
  if (! opt_state->xml)
    SVN_ERR(svn_stream_for_stdout(&bl.out, pool));
  else
    bl.sbuf = svn_stringbuf_create_empty(pool);

  bl.opt_state = opt_state;
  bl.rev_maxlength = 0;

  subpool = svn_pool_create(pool);

  if (opt_state->extensions)
    {
      apr_array_header_t *opts;
      opts = svn_cstring_split(opt_state->extensions, " \t\n\r", TRUE, pool);
      SVN_ERR(svn_diff_file_options_parse(diff_options, opts, pool));
    }

  if (opt_state->xml)
    {
      if (opt_state->verbose)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("'verbose' option invalid in XML mode"));

      /* If output is not incremental, output the XML header and wrap
         everything in a top-level element.  This makes the output in
         its entirety a well-formed XML document. */
      if (! opt_state->incremental)
        SVN_ERR(svn_cl__xml_print_header("blame", pool));
    }
  else
    {
      if (opt_state->incremental)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("'incremental' option only valid in XML "
                                  "mode"));
    }

  for (i = 0; i < targets->nelts; i++)
    {
      svn_error_t *err;
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      const char *truepath;
      svn_opt_revision_t peg_revision;
      svn_client_blame_receiver3_t receiver;

      svn_pool_clear(subpool);
      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      /* Check for a peg revision. */
      SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target,
                                 subpool));

      if (end_revision_unspecified)
        {
          if (peg_revision.kind != svn_opt_revision_unspecified)
            opt_state->end_revision = peg_revision;
          else if (svn_path_is_url(target))
            opt_state->end_revision.kind = svn_opt_revision_head;
          else
            opt_state->end_revision.kind = svn_opt_revision_working;
        }

      if (opt_state->xml)
        {
          /* "<target ...>" */
          /* We don't output this tag immediately, which avoids creating
             a target element if this path is skipped. */
          const char *outpath = truepath;
          if (! svn_path_is_url(target))
            outpath = svn_dirent_local_style(truepath, subpool);
          svn_xml_make_open_tag(&bl.sbuf, pool, svn_xml_normal, "target",
                                "path", outpath, SVN_VA_NULL);

          receiver = blame_receiver_xml;
        }
      else
        receiver = blame_receiver;

      err = svn_client_blame5(truepath,
                              &peg_revision,
                              &opt_state->start_revision,
                              &opt_state->end_revision,
                              diff_options,
                              opt_state->force,
                              opt_state->use_merge_history,
                              receiver,
                              &bl,
                              ctx,
                              subpool);

      if (err)
        {
          if (err->apr_err == SVN_ERR_CLIENT_IS_BINARY_FILE)
            {
              svn_error_clear(err);
              SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
                                          _("Skipping binary file "
                                            "(use --force to treat as text): "
                                            "'%s'\n"),
                                          target));
            }
          else if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND ||
                   err->apr_err == SVN_ERR_ENTRY_NOT_FOUND ||
                   err->apr_err == SVN_ERR_FS_NOT_FILE ||
                   err->apr_err == SVN_ERR_FS_NOT_FOUND)
            {
              svn_handle_warning2(stderr, err, "svn: ");
              svn_error_clear(err);
              err = NULL;
              seen_nonexistent_target = TRUE;
            }
          else
            {
              return svn_error_trace(err);
            }
        }
      else if (opt_state->xml)
        {
          /* "</target>" */
          svn_xml_make_close_tag(&(bl.sbuf), pool, "target");
          SVN_ERR(svn_cl__error_checked_fputs(bl.sbuf->data, stdout));
        }

      if (opt_state->xml)
        svn_stringbuf_setempty(bl.sbuf);
    }
  svn_pool_destroy(subpool);
  if (opt_state->xml && ! opt_state->incremental)
    SVN_ERR(svn_cl__xml_print_footer("blame", pool));

  if (seen_nonexistent_target)
    return svn_error_create(
      SVN_ERR_ILLEGAL_TARGET, NULL,
      _("Could not perform blame on all targets because some "
        "targets don't exist"));
  else
    return SVN_NO_ERROR;
}
