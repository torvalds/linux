/*
 * switch-cmd.c -- Bring work tree in sync with a different URL
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

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "cl.h"

#include "svn_private_config.h"

/*** Code. ***/

static svn_error_t *
rewrite_urls(const apr_array_header_t *targets,
             svn_boolean_t ignore_externals,
             svn_client_ctx_t *ctx,
             apr_pool_t *pool)
{
  apr_pool_t *subpool;
  const char *from;
  const char *to;

  if (targets->nelts < 2)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  from = APR_ARRAY_IDX(targets, 0, const char *);
  to = APR_ARRAY_IDX(targets, 1, const char *);

  /* "--relocate http https" and "--relocate http://foo svn://bar" are OK,
     but things like "--relocate http://foo svn" are not */
  if (svn_path_is_url(from) != svn_path_is_url(to))
    return svn_error_createf
      (SVN_ERR_INCORRECT_PARAMS, NULL,
       _("'%s' to '%s' is not a valid relocation"), from, to);

  subpool = svn_pool_create(pool);

  if (targets->nelts == 2)
    {
      SVN_ERR(svn_client_relocate2("", from, to, ignore_externals,
                                   ctx, pool));
    }
  else
    {
      int i;

      for (i = 2; i < targets->nelts; i++)
        {
          const char *target = APR_ARRAY_IDX(targets, i, const char *);
          svn_pool_clear(subpool);
          SVN_ERR(svn_client_relocate2(target, from, to,
                                       ignore_externals, ctx, subpool));
        }
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__switch(apr_getopt_t *os,
               void *baton,
               apr_pool_t *scratch_pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  svn_error_t *externals_err = SVN_NO_ERROR;
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_cl__conflict_stats_t *conflict_stats =
    ((svn_cl__cmd_baton_t *) baton)->conflict_stats;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  apr_array_header_t *conflicted_paths;
  const char *target, *switch_url;
  svn_opt_revision_t peg_revision;
  svn_depth_t depth;
  svn_boolean_t depth_is_sticky;
  struct svn_cl__check_externals_failed_notify_baton nwb;

  /* This command should discover (or derive) exactly two cmdline
     arguments: a local path to update ("target"), and a new url to
     switch to ("switch_url"). */
  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE,
                                                      scratch_pool));

  /* handle only-rewrite case specially */
  if (opt_state->relocate)
    return rewrite_urls(targets, opt_state->ignore_externals,
                        ctx, scratch_pool);

  if (targets->nelts < 1)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
  if (targets->nelts > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  /* Get the required SWITCH_URL and its optional PEG_REVISION, and the
   * optional TARGET argument. */
  SVN_ERR(svn_opt_parse_path(&peg_revision, &switch_url,
                             APR_ARRAY_IDX(targets, 0, const char *),
                             scratch_pool));
  if (targets->nelts == 1)
    target = "";
  else
    target = APR_ARRAY_IDX(targets, 1, const char *);

  /* Validate the switch_url */
  if (! svn_path_is_url(switch_url))
    return svn_error_createf(SVN_ERR_BAD_URL, NULL,
                             _("'%s' does not appear to be a URL"), switch_url);

  SVN_ERR(svn_cl__check_target_is_local_path(target));

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

  /* Do the 'switch' update. */
  err = svn_client_switch3(NULL, target, switch_url, &peg_revision,
                           &(opt_state->start_revision), depth,
                           depth_is_sticky, opt_state->ignore_externals,
                           opt_state->force, opt_state->ignore_ancestry,
                           ctx, scratch_pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_CLIENT_UNRELATED_RESOURCES)
        return svn_error_createf(SVN_ERR_CLIENT_UNRELATED_RESOURCES, err,
                                 _("Path '%s' does not share common version "
                                   "control ancestry with the requested switch "
                                   "location.  Use --ignore-ancestry to "
                                   "disable this check."),
                                   svn_dirent_local_style(target,
                                                          scratch_pool));
      if (err->apr_err == SVN_ERR_RA_UUID_MISMATCH
          || err->apr_err == SVN_ERR_WC_INVALID_SWITCH)
        return svn_error_quick_wrap(
                 err,
                 _("'svn switch' does not support switching a working copy to "
                   "a different repository"));
      return err;
    }

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
      err = svn_cl__notifier_print_conflict_stats(nwb.wrapped_baton, scratch_pool);
      if (err)
        return svn_error_compose_create(externals_err, err);
    }

  return svn_error_compose_create(externals_err, err);
}
