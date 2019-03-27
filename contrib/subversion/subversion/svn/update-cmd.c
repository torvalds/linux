/*
 * update-cmd.c -- Bring work tree in sync with repository
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

#include "svn_pools.h"
#include "svn_client.h"
#include "svn_path.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* Print an update summary when there's more than one target to report
   about.  Each (const char *) path in TARGETS is an absolute or relative
   dirent, and each (svn_revnum_t) entry in RESULT_REVS is the corresponding
   updated revision, or SVN_INVALID_REVNUM if not a valid target. */
static svn_error_t *
print_update_summary(apr_array_header_t *targets,
                     apr_array_header_t *result_revs,
                     apr_pool_t *scratch_pool)
{
  int i;
  const char *path_prefix;
  apr_pool_t *iterpool;
  svn_boolean_t printed_header = FALSE;

  if (targets->nelts < 2)
    return SVN_NO_ERROR;

  SVN_ERR(svn_dirent_get_absolute(&path_prefix, "", scratch_pool));

  iterpool = svn_pool_create(scratch_pool);

  for (i = 0; i < targets->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(targets, i, const char *);
      svn_revnum_t rev = SVN_INVALID_REVNUM;

      svn_pool_clear(iterpool);

      /* PATH shouldn't be a URL. */
      SVN_ERR_ASSERT(! svn_path_is_url(path));

      /* Grab the result revision from the corresponding slot in our
         RESULT_REVS array. */
      if (i < result_revs->nelts)
        rev = APR_ARRAY_IDX(result_revs, i, svn_revnum_t);

      /* No result rev?  We must have skipped this path.  At any rate,
         nothing to report here. */
      if (! SVN_IS_VALID_REVNUM(rev))
        continue;

      /* Convert to an absolute path if it's not already. */
      if (! svn_dirent_is_absolute(path))
        SVN_ERR(svn_dirent_get_absolute(&path, path, iterpool));

      /* Print an update summary for this target, removing the current
         working directory prefix from PATH (if PATH is at or under
         $CWD), and converting the path to local style for display. */
      if (! printed_header)
        {
          SVN_ERR(svn_cmdline_printf(scratch_pool,
                                     _("Summary of updates:\n")));
          printed_header = TRUE;
        }

      SVN_ERR(svn_cmdline_printf(iterpool, _("  Updated '%s' to r%ld.\n"),
                                 svn_cl__local_style_skip_ancestor(
                                   path_prefix, path, iterpool),
                                 rev));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__update(apr_getopt_t *os,
               void *baton,
               apr_pool_t *scratch_pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_cl__conflict_stats_t *conflict_stats =
    ((svn_cl__cmd_baton_t *) baton)->conflict_stats;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  svn_depth_t depth;
  svn_boolean_t depth_is_sticky;
  struct svn_cl__check_externals_failed_notify_baton nwb;
  apr_array_header_t *result_revs;
  apr_array_header_t *conflicted_paths;
  svn_error_t *err = SVN_NO_ERROR;
  svn_error_t *externals_err = SVN_NO_ERROR;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE,
                                                      scratch_pool));

  /* Add "." if user passed 0 arguments */
  svn_opt_push_implicit_dot_target(targets, scratch_pool);

  SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, scratch_pool));

  SVN_ERR(svn_cl__check_targets_are_local_paths(targets));

  /* If using changelists, convert targets into a set of paths that
     match the specified changelist(s). */
  if (opt_state->changelists)
    {
      svn_depth_t cl_depth = opt_state->depth;
      if (cl_depth == svn_depth_unknown)
        cl_depth = svn_depth_infinity;
      SVN_ERR(svn_cl__changelist_paths(&targets,
                                       opt_state->changelists, targets,
                                       cl_depth, ctx, scratch_pool,
                                       scratch_pool));
    }

  /* Deal with depthstuffs. */
  if (opt_state->set_depth != svn_depth_unknown)
    {
      depth = opt_state->set_depth;
      depth_is_sticky = TRUE;
    }
  else
    {
      depth = opt_state->depth;
      depth_is_sticky = FALSE;
    }

  nwb.wrapped_func = ctx->notify_func2;
  nwb.wrapped_baton = ctx->notify_baton2;
  nwb.had_externals_error = FALSE;
  ctx->notify_func2 = svn_cl__check_externals_failed_notify_wrapper;
  ctx->notify_baton2 = &nwb;

  SVN_ERR(svn_client_update4(&result_revs, targets,
                             &(opt_state->start_revision),
                             depth, depth_is_sticky,
                             opt_state->ignore_externals,
                             opt_state->force,
                             opt_state->adds_as_modification,
                             opt_state->parents,
                             ctx, scratch_pool));

  if (nwb.had_externals_error)
    externals_err = svn_error_create(SVN_ERR_CL_ERROR_PROCESSING_EXTERNALS,
                                     NULL,
                                     _("Failure occurred processing one or "
                                       "more externals definitions"));

  /* Run the interactive resolver if conflicts were raised. */
  SVN_ERR(svn_cl__conflict_stats_get_paths(&conflicted_paths, conflict_stats,
                                           scratch_pool, scratch_pool));
  if (conflicted_paths)
    SVN_ERR(svn_cl__walk_conflicts(conflicted_paths, conflict_stats,
                                   opt_state, ctx, scratch_pool));

  if (! opt_state->quiet)
    {
      err = print_update_summary(targets, result_revs, scratch_pool);
      if (err)
        return svn_error_compose_create(externals_err, err);

      /* ### Layering problem: This call assumes that the baton we're
       * passing is the one that was originally provided by
       * svn_cl__get_notifier(), but that isn't promised. */
      err = svn_cl__notifier_print_conflict_stats(nwb.wrapped_baton,
                                                  scratch_pool);
      if (err)
        return svn_error_compose_create(externals_err, err);
    }

  return svn_error_compose_create(externals_err, err);
}
