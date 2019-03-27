/*
 * null-blame-cmd.c -- Subversion export command
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

#include "svn_client.h"
#include "svn_cmdline.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_sorts.h"
#include "cl.h"

#include "svn_private_config.h"
#include "private/svn_string_private.h"
#include "private/svn_client_private.h"

struct file_rev_baton {
  apr_int64_t byte_count;
  apr_int64_t delta_count;
  apr_int64_t rev_count;
};

/* Implements svn_txdelta_window_handler_t */
static svn_error_t *
delta_handler(svn_txdelta_window_t *window, void *baton)
{
  struct file_rev_baton *frb = baton;

  if (window != NULL)
    frb->byte_count += window->tview_len;

  return SVN_NO_ERROR;
}

/* Implementes svn_file_rev_handler_t */
static svn_error_t *
file_rev_handler(void *baton, const char *path, svn_revnum_t revnum,
                 apr_hash_t *rev_props,
                 svn_boolean_t merged_revision,
                 svn_txdelta_window_handler_t *content_delta_handler,
                 void **content_delta_baton,
                 apr_array_header_t *prop_diffs,
                 apr_pool_t *pool)
{
  struct file_rev_baton *frb = baton;

  frb->rev_count++;

  if (content_delta_handler)
    {
      *content_delta_handler = delta_handler;
      *content_delta_baton = baton;
      frb->delta_count++;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
bench_null_blame(const char *target,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *start,
                 const svn_opt_revision_t *end,
                 svn_boolean_t include_merged_revisions,
                 svn_boolean_t quiet,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  struct file_rev_baton frb = { 0, 0, 0};
  svn_ra_session_t *ra_session;
  svn_revnum_t start_revnum, end_revnum;
  svn_boolean_t backwards;
  const char *target_abspath_or_url;

  if (start->kind == svn_opt_revision_unspecified
      || end->kind == svn_opt_revision_unspecified)
    return svn_error_create
      (SVN_ERR_CLIENT_BAD_REVISION, NULL, NULL);

  if (svn_path_is_url(target))
    target_abspath_or_url = target;
  else
    SVN_ERR(svn_dirent_get_absolute(&target_abspath_or_url, target, pool));


  /* Get an RA plugin for this filesystem object. */
  SVN_ERR(svn_client__ra_session_from_path2(&ra_session, NULL,
                                            target, NULL, peg_revision,
                                            peg_revision,
                                            ctx, pool));

  SVN_ERR(svn_client__get_revision_number(&start_revnum, NULL, ctx->wc_ctx,
                                          target_abspath_or_url, ra_session,
                                          start, pool));

  SVN_ERR(svn_client__get_revision_number(&end_revnum, NULL, ctx->wc_ctx,
                                          target_abspath_or_url, ra_session,
                                          end, pool));

  {
    svn_client__pathrev_t *loc;
    svn_opt_revision_t younger_end;
    younger_end.kind = svn_opt_revision_number;
    younger_end.value.number = MAX(start_revnum, end_revnum);

    SVN_ERR(svn_client__resolve_rev_and_url(&loc, ra_session,
                                            target, peg_revision,
                                            &younger_end,
                                            ctx, pool));

    /* Make the session point to the real URL. */
    SVN_ERR(svn_ra_reparent(ra_session, loc->url, pool));
  }

  backwards = (start_revnum > end_revnum);

  /* Collect all blame information.
     We need to ensure that we get one revision before the start_rev,
     if available so that we can know what was actually changed in the start
     revision. */
  SVN_ERR(svn_ra_get_file_revs2(ra_session, "",
                                backwards ? start_revnum
                                          : MAX(0, start_revnum-1),
                                end_revnum,
                                include_merged_revisions,
                                file_rev_handler, &frb, pool));

  if (!quiet)
    SVN_ERR(svn_cmdline_printf(pool,
                               _("%15s revisions\n"
                                 "%15s deltas\n"
                                 "%15s bytes in deltas\n"),
                               svn__ui64toa_sep(frb.rev_count, ',', pool),
                               svn__ui64toa_sep(frb.delta_count, ',', pool),
                               svn__ui64toa_sep(frb.byte_count, ',', pool)));

  return SVN_NO_ERROR;
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__null_blame(apr_getopt_t *os,
                   void *baton,
                   apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_pool_t *iterpool;
  apr_array_header_t *targets;
  int i;
  svn_boolean_t end_revision_unspecified = FALSE;
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
  iterpool = svn_pool_create(pool);

  for (i = 0; i < targets->nelts; i++)
    {
      svn_error_t *err;
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      const char *parsed_path;
      svn_opt_revision_t peg_revision;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      /* Check for a peg revision. */
      SVN_ERR(svn_opt_parse_path(&peg_revision, &parsed_path, target,
                                 iterpool));

      if (end_revision_unspecified)
        {
          if (peg_revision.kind != svn_opt_revision_unspecified)
            opt_state->end_revision = peg_revision;
          else if (svn_path_is_url(target))
            opt_state->end_revision.kind = svn_opt_revision_head;
          else
            opt_state->end_revision.kind = svn_opt_revision_working;
        }

      err = bench_null_blame(parsed_path,
                             &peg_revision,
                             &opt_state->start_revision,
                             &opt_state->end_revision,
                             opt_state->use_merge_history,
                             opt_state->quiet,
                             ctx,
                             iterpool);

      if (err)
        {
          if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND ||
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
    }
  svn_pool_destroy(iterpool);

  if (seen_nonexistent_target)
    return svn_error_create(
      SVN_ERR_ILLEGAL_TARGET, NULL,
      _("Could not perform blame on all targets because some "
        "targets don't exist"));
  else
    return SVN_NO_ERROR;
}
