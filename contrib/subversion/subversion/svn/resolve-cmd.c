/*
 * resolve-cmd.c -- Subversion resolve subcommand
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
#include "svn_path.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_hash.h"
#include "cl.h"

#include "svn_private_config.h"



/*** Code. ***/

struct conflict_walker_baton
{
  svn_client_ctx_t *ctx;
  svn_cl__accept_t accept_which;
  svn_boolean_t quit;
  svn_boolean_t external_failed;
  svn_boolean_t printed_summary;
  const char *editor_cmd;
  const char *path_prefix;
  svn_cmdline_prompt_baton_t *pb;
  svn_cl__conflict_stats_t *conflict_stats;
};

/* Implements svn_client_conflict_walk_func_t. */
static svn_error_t *
conflict_walker(void *baton, svn_client_conflict_t *conflict,
                apr_pool_t *scratch_pool)
{
  struct conflict_walker_baton *cwb = baton;

  SVN_ERR(svn_cl__resolve_conflict(&cwb->quit, &cwb->external_failed,
                                   &cwb->printed_summary, conflict,
                                   cwb->accept_which, cwb->editor_cmd,
                                   cwb->path_prefix, cwb->pb,
                                   cwb->conflict_stats,
                                   cwb->ctx, scratch_pool));
  if (cwb->quit)
    return svn_error_create(SVN_ERR_CANCELLED, NULL, NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_cl__walk_conflicts(apr_array_header_t *targets,
                       svn_cl__conflict_stats_t *conflict_stats,
                       svn_cl__opt_state_t *opt_state,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *scratch_pool)
{
  svn_boolean_t had_error = FALSE;
  svn_cmdline_prompt_baton_t *pb = apr_palloc(scratch_pool, sizeof(*pb));
  struct conflict_walker_baton cwb = { 0 };
  const char *path_prefix;
  svn_error_t *err;
  int i;
  apr_pool_t *iterpool;

  SVN_ERR(svn_dirent_get_absolute(&path_prefix, "", scratch_pool));

  pb->cancel_func = ctx->cancel_func;
  pb->cancel_baton = ctx->cancel_baton;

  cwb.ctx = ctx;
  cwb.accept_which = opt_state->accept_which;
  cwb.quit = FALSE;
  cwb.external_failed = FALSE;
  cwb.printed_summary = FALSE;
  cwb.editor_cmd = opt_state->editor_cmd;
  cwb.path_prefix = path_prefix;
  cwb.pb = pb;
  cwb.conflict_stats = conflict_stats;

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      const char *local_abspath;
      svn_client_conflict_t *conflict;

      svn_pool_clear(iterpool);
 
      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      SVN_ERR(svn_dirent_get_absolute(&local_abspath, target, iterpool));

      if (opt_state->depth == svn_depth_empty)
        {
          SVN_ERR(svn_client_conflict_get(&conflict, local_abspath, ctx,
                                          iterpool, iterpool));
          err = svn_cl__resolve_conflict(&cwb.quit, &cwb.external_failed,
                                         &cwb.printed_summary,
                                         conflict, opt_state->accept_which,
                                         opt_state->editor_cmd,
                                         path_prefix, pb, conflict_stats,
                                         ctx, iterpool);
        }
      else
        err = svn_client_conflict_walk(local_abspath, opt_state->depth,
                                       conflict_walker, &cwb, ctx, iterpool);

      if (err)
        {
          svn_error_t *root = svn_error_root_cause(err);

          if (root->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
            {
              /* ### Ignore. These errors can happen due to the working copy
               * ### being re-arranged during tree conflict resolution. */
              svn_error_clear(err);
              continue;
            }
          else if (root->apr_err == SVN_ERR_CANCELLED)
            {
              svn_error_clear(err);
              break;
            }

          svn_handle_warning2(stderr, svn_error_root_cause(err), "svn: ");
          svn_error_clear(err);
          had_error = TRUE;
        }
    }
  svn_pool_destroy(iterpool);

  if (had_error)
    return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                            _("Failure occurred resolving one or more "
                              "conflicts"));
  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__resolve(apr_getopt_t *os,
                void *baton,
                apr_pool_t *scratch_pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_cl__conflict_stats_t *conflict_stats =
    ((svn_cl__cmd_baton_t *) baton)->conflict_stats;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE,
                                                      scratch_pool));
  if (! targets->nelts)
    svn_opt_push_implicit_dot_target(targets, scratch_pool);

  if (opt_state->depth == svn_depth_unknown)
    {
      if (opt_state->accept_which == svn_cl__accept_unspecified)
        opt_state->depth = svn_depth_infinity;
      else
        opt_state->depth = svn_depth_empty;
    }

  SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, scratch_pool));

  SVN_ERR(svn_cl__check_targets_are_local_paths(targets));

  if (opt_state->accept_which == svn_cl__accept_unspecified &&
      opt_state->non_interactive)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("missing --accept option"));
    }
  else if (opt_state->accept_which == svn_cl__accept_postpone ||
           opt_state->accept_which == svn_cl__accept_edit ||
           opt_state->accept_which == svn_cl__accept_launch)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("invalid 'accept' ARG"));
    }

  SVN_ERR(svn_cl__walk_conflicts(targets, conflict_stats,
                                 opt_state, ctx, scratch_pool));

  return SVN_NO_ERROR;
}
