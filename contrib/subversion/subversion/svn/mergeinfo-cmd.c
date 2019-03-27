/*
 * mergeinfo-cmd.c -- Query merge-relative info.
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_compat.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_client.h"
#include "svn_cmdline.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_types.h"
#include "cl.h"
#include "cl-log.h"

#include "svn_private_config.h"


/*** Code. ***/

/* Implements the svn_log_entry_receiver_t interface. */
static svn_error_t *
print_log_rev(void *baton,
              svn_log_entry_t *log_entry,
              apr_pool_t *pool)
{
  if (log_entry->non_inheritable)
    SVN_ERR(svn_cmdline_printf(pool, "r%ld*\n", log_entry->revision));
  else
    SVN_ERR(svn_cmdline_printf(pool, "r%ld\n", log_entry->revision));

  return SVN_NO_ERROR;
}

/* Implements a svn_log_entry_receiver_t interface that filters out changed
 * paths data before calling the svn_cl__log_entry_receiver().  Right now we
 * always have to pass TRUE for discover_changed_paths for
 * svn_client_mergeinfo_log2() due to the side effect of that option.  The
 * svn_cl__log_entry_receiver() discovers if it should print the changed paths
 * implicitly by the path info existing.  As a result this filter is needed
 * to allow expected output without changed paths.
 */
static svn_error_t *
print_log_details(void *baton,
                  svn_log_entry_t *log_entry,
                  apr_pool_t *pool)
{
  log_entry->changed_paths = NULL;
  log_entry->changed_paths2 = NULL;

  return svn_cl__log_entry_receiver(baton, log_entry, pool);
}

/* Draw a diagram (by printing text to the console) summarizing the state
 * of merging between two branches, given the merge description
 * indicated by YCA, BASE, RIGHT, TARGET, REINTEGRATE_LIKE. */
static svn_error_t *
mergeinfo_diagram(const char *yca_url,
                  const char *base_url,
                  const char *right_url,
                  const char *target_url,
                  svn_revnum_t yca_rev,
                  svn_revnum_t base_rev,
                  svn_revnum_t right_rev,
                  svn_revnum_t target_rev,
                  const char *repos_root_url,
                  svn_boolean_t target_is_wc,
                  svn_boolean_t reintegrate_like,
                  apr_pool_t *pool)
{
  /* The graph occupies 4 rows of text, and the annotations occupy
   * another 2 rows above and 2 rows below.  The graph is constructed
   * from left to right in discrete sections ("columns"), each of which
   * can have a different width (measured in characters).  Each element in
   * the array is either a text string of the appropriate width, or can
   * be NULL to draw a blank cell. */
#define ROWS 8
#define COLS 4
  const char *g[ROWS][COLS] = {{0}};
  int col_width[COLS];
  int row, col;

  /* The YCA (that is, the branching point).  And an ellipsis, because we
   * don't show information about earlier merges */
  g[0][0] = apr_psprintf(pool, "  %-8ld  ", yca_rev);
  g[1][0] =     "  |         ";
  if (strcmp(yca_url, right_url) == 0)
    {
      g[2][0] = "-------| |--";
      g[3][0] = "   \\        ";
      g[4][0] = "    \\       ";
      g[5][0] = "     --| |--";
    }
  else if (strcmp(yca_url, target_url) == 0)
    {
      g[2][0] = "     --| |--";
      g[3][0] = "    /       ";
      g[4][0] = "   /        ";
      g[5][0] = "-------| |--";
    }
  else
    {
      g[2][0] = "     --| |--";
      g[3][0] = "... /       ";
      g[4][0] = "    \\       ";
      g[5][0] = "     --| |--";
    }

  /* The last full merge */
  if ((base_rev > yca_rev) && reintegrate_like)
    {
      g[2][2] = "---------";
      g[3][2] = "  /      ";
      g[4][2] = " /       ";
      g[5][2] = "---------";
      g[6][2] = "|        ";
      g[7][2] = apr_psprintf(pool, "%-8ld ", base_rev);
    }
  else if (base_rev > yca_rev)
    {
      g[0][2] = apr_psprintf(pool, "%-8ld ", base_rev);
      g[1][2] = "|        ";
      g[2][2] = "---------";
      g[3][2] = " \\       ";
      g[4][2] = "  \\      ";
      g[5][2] = "---------";
    }
  else
    {
      g[2][2] = "---------";
      g[3][2] = "         ";
      g[4][2] = "         ";
      g[5][2] = "---------";
    }

  /* The tips of the branches */
    {
      g[0][3] = apr_psprintf(pool, "%-8ld", right_rev);
      g[1][3] = "|       ";
      g[2][3] = "-       ";
      g[3][3] = "        ";
      g[4][3] = "        ";
      g[5][3] = "-       ";
      g[6][3] = "|       ";
      g[7][3] = target_is_wc ? "WC      "
                             : apr_psprintf(pool, "%-8ld", target_rev);
    }

  /* Find the width of each column, so we know how to print blank cells */
  for (col = 0; col < COLS; col++)
    {
      col_width[col] = 0;
      for (row = 0; row < ROWS; row++)
        {
          if (g[row][col] && ((int)strlen(g[row][col]) > col_width[col]))
            col_width[col] = (int)strlen(g[row][col]);
        }
    }

  /* Column headings */
  SVN_ERR(svn_cmdline_printf(pool,
            "    %s\n"
            "    |         %s\n"
            "    |         |        %s\n"
            "    |         |        |         %s\n"
            "\n",
            _("youngest common ancestor"), _("last full merge"),
            _("tip of branch"), _("repository path")));

  /* Print the diagram, row by row */
  for (row = 0; row < ROWS; row++)
    {
      SVN_ERR(svn_cmdline_fputs("  ", stdout, pool));
      for (col = 0; col < COLS; col++)
        {
          if (g[row][col])
            {
              SVN_ERR(svn_cmdline_fputs(g[row][col], stdout, pool));
            }
          else
            {
              /* Print <column-width> spaces */
              SVN_ERR(svn_cmdline_printf(pool, "%*s", col_width[col], ""));
            }
        }
      if (row == 2)
        SVN_ERR(svn_cmdline_printf(pool, "  %s",
                svn_uri_skip_ancestor(repos_root_url, right_url, pool)));
      if (row == 5)
        SVN_ERR(svn_cmdline_printf(pool, "  %s",
                svn_uri_skip_ancestor(repos_root_url, target_url, pool)));
      SVN_ERR(svn_cmdline_fputs("\n", stdout, pool));
    }

  return SVN_NO_ERROR;
}

/* Display a summary of the state of merging between the two branches
 * SOURCE_PATH_OR_URL@SOURCE_REVISION and
 * TARGET_PATH_OR_URL@TARGET_REVISION. */
static svn_error_t *
mergeinfo_summary(
                  const char *source_path_or_url,
                  const svn_opt_revision_t *source_revision,
                  const char *target_path_or_url,
                  const svn_opt_revision_t *target_revision,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  const char *yca_url, *base_url, *right_url, *target_url;
  svn_revnum_t yca_rev, base_rev, right_rev, target_rev;
  const char *repos_root_url;
  svn_boolean_t target_is_wc, is_reintegration;

  target_is_wc = (! svn_path_is_url(target_path_or_url))
                 && (target_revision->kind == svn_opt_revision_unspecified
                     || target_revision->kind == svn_opt_revision_working
                     || target_revision->kind == svn_opt_revision_base);
  SVN_ERR(svn_client_get_merging_summary(
            &is_reintegration,
            &yca_url, &yca_rev,
            &base_url, &base_rev,
            &right_url, &right_rev,
            &target_url, &target_rev,
            &repos_root_url,
            source_path_or_url, source_revision,
            target_path_or_url, target_revision,
            ctx, pool, pool));

  SVN_ERR(mergeinfo_diagram(yca_url, base_url, right_url, target_url,
                            yca_rev, base_rev, right_rev, target_rev,
                            repos_root_url, target_is_wc, is_reintegration,
                            pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
mergeinfo_log(svn_boolean_t finding_merged,
              const char *target,
              const svn_opt_revision_t *tgt_peg_revision,
              const char *source,
              const svn_opt_revision_t *src_peg_revision,
              const svn_opt_revision_t *src_start_revision,
              const svn_opt_revision_t *src_end_revision,
              svn_depth_t depth,
              svn_boolean_t include_log_details,
              svn_boolean_t quiet,
              svn_boolean_t verbose,
              svn_boolean_t incremental,
              svn_client_ctx_t *ctx,
              apr_pool_t *pool)
{
  apr_array_header_t *revprops;
  svn_log_entry_receiver_t log_receiver;
  void *log_receiver_baton;

  if (include_log_details)
    {
      svn_cl__log_receiver_baton *baton;

      revprops = apr_array_make(pool, 3, sizeof(const char *));
      APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_AUTHOR;
      APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_DATE;
      if (!quiet)
        APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_LOG;

      if (verbose)
        log_receiver = svn_cl__log_entry_receiver;
      else
        log_receiver = print_log_details;

      baton = apr_palloc(pool, sizeof(svn_cl__log_receiver_baton));
      baton->ctx = ctx;
      baton->target_path_or_url = target;
      baton->target_peg_revision = *tgt_peg_revision;
      baton->omit_log_message = quiet;
      baton->show_diff = FALSE;
      baton->depth = depth;
      baton->diff_extensions = NULL;
      baton->merge_stack = NULL;
      baton->search_patterns = NULL;
      baton->pool = pool;
      log_receiver_baton = baton;
    }
  else
    {
      /* We need only revisions number, not revision properties. */
      revprops = apr_array_make(pool, 0, sizeof(const char *));
      log_receiver = print_log_rev;
      log_receiver_baton = NULL;
    }

  SVN_ERR(svn_client_mergeinfo_log2(finding_merged, target,
                                    tgt_peg_revision,
                                    source, src_peg_revision,
                                    src_start_revision,
                                    src_end_revision,
                                    log_receiver, log_receiver_baton,
                                    TRUE, depth, revprops, ctx,
                                    pool));

  if (include_log_details && !incremental)
    SVN_ERR(svn_cmdline_printf(pool, SVN_CL__LOG_SEP_STRING));

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__mergeinfo(apr_getopt_t *os,
                  void *baton,
                  apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  const char *source, *target;
  svn_opt_revision_t src_peg_revision, tgt_peg_revision;
  svn_opt_revision_t *src_start_revision, *src_end_revision;
  /* Default to depth empty. */
  svn_depth_t depth = (opt_state->depth == svn_depth_unknown)
                      ? svn_depth_empty : opt_state->depth;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* Parse the arguments: SOURCE[@REV] optionally followed by TARGET[@REV]. */
  if (targets->nelts < 1)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Not enough arguments given"));
  if (targets->nelts > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Too many arguments given"));
  SVN_ERR(svn_opt_parse_path(&src_peg_revision, &source,
                             APR_ARRAY_IDX(targets, 0, const char *), pool));
  if (targets->nelts == 2)
    {
      SVN_ERR(svn_opt_parse_path(&tgt_peg_revision, &target,
                                 APR_ARRAY_IDX(targets, 1, const char *),
                                 pool));
    }
  else
    {
      target = "";
      tgt_peg_revision.kind = svn_opt_revision_unspecified;
    }

  /* If no peg-rev was attached to the source URL, assume HEAD. */
  /* ### But what if SOURCE is a WC path not a URL -- shouldn't we then use
   *     BASE (but not WORKING: that would be inconsistent with 'svn merge')? */
  if (src_peg_revision.kind == svn_opt_revision_unspecified)
    src_peg_revision.kind = svn_opt_revision_head;

  /* If no peg-rev was attached to a URL target, then assume HEAD; if
     no peg-rev was attached to a non-URL target, then assume BASE. */
  /* ### But we would like to be able to examine a working copy with an
         uncommitted merge in it, so change this to use WORKING not BASE? */
  if (tgt_peg_revision.kind == svn_opt_revision_unspecified)
    {
      if (svn_path_is_url(target))
        tgt_peg_revision.kind = svn_opt_revision_head;
      else
        tgt_peg_revision.kind = svn_opt_revision_base;
    }

  src_start_revision = &(opt_state->start_revision);
  if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
    src_end_revision = src_start_revision;
  else
    src_end_revision = &(opt_state->end_revision);

  if (!opt_state->mergeinfo_log)
    {
      if (opt_state->quiet)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("--quiet (-q) option valid only with --log "
                                  "option"));

      if (opt_state->verbose)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("--verbose (-v) option valid only with "
                                  "--log option"));

      if (opt_state->incremental)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("--incremental option valid only with "
                                  "--log option"));
    }

  /* Do the real work, depending on the requested data flavor. */
  if (opt_state->show_revs == svn_cl__show_revs_merged)
    {
      SVN_ERR(mergeinfo_log(TRUE, target, &tgt_peg_revision,
                            source, &src_peg_revision,
                            src_start_revision,
                            src_end_revision,
                            depth, opt_state->mergeinfo_log,
                            opt_state->quiet, opt_state->verbose,
                            opt_state->incremental, ctx, pool));
    }
  else if (opt_state->show_revs == svn_cl__show_revs_eligible)
    {
      SVN_ERR(mergeinfo_log(FALSE, target, &tgt_peg_revision,
                            source, &src_peg_revision,
                            src_start_revision,
                            src_end_revision,
                            depth, opt_state->mergeinfo_log,
                            opt_state->quiet, opt_state->verbose,
                            opt_state->incremental, ctx, pool));
    }
  else
    {
      if ((opt_state->start_revision.kind != svn_opt_revision_unspecified)
          || (opt_state->end_revision.kind != svn_opt_revision_unspecified))
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("--revision (-r) option valid only with "
                                  "--show-revs option"));
      if (opt_state->depth != svn_depth_unknown)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Depth specification options valid only "
                                  "with --show-revs option"));
      if (opt_state->mergeinfo_log)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("--log option valid only with "
                                  "--show-revs option"));


      SVN_ERR(mergeinfo_summary(source, &src_peg_revision,
                                target, &tgt_peg_revision,
                                ctx, pool));
    }
  return SVN_NO_ERROR;
}
