/*
 * patch-cmd.c -- Apply changes to a working copy.
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
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_types.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__patch(apr_getopt_t *os,
              void *baton,
              apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state;
  svn_client_ctx_t *ctx;
  apr_array_header_t *targets;
  const char *abs_patch_path;
  const char *patch_path;
  const char *abs_target_path;
  const char *target_path;

  opt_state = ((svn_cl__cmd_baton_t *)baton)->opt_state;
  ctx = ((svn_cl__cmd_baton_t *)baton)->ctx;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));
  SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, pool));

  if (targets->nelts < 1)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  if (targets->nelts > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  patch_path = APR_ARRAY_IDX(targets, 0, const char *);

  SVN_ERR(svn_cl__check_target_is_local_path(patch_path));

  SVN_ERR(svn_dirent_get_absolute(&abs_patch_path, patch_path, pool));

  if (targets->nelts == 1)
    target_path = ""; /* "" is the canonical form of "." */
  else
    {
      target_path = APR_ARRAY_IDX(targets, 1, const char *);

      SVN_ERR(svn_cl__check_target_is_local_path(target_path));
    }
  SVN_ERR(svn_dirent_get_absolute(&abs_target_path, target_path, pool));

  SVN_ERR(svn_client_patch(abs_patch_path, abs_target_path,
                           opt_state->dry_run, opt_state->strip,
                           opt_state->reverse_diff,
                           opt_state->ignore_whitespace,
                           TRUE, NULL, NULL, ctx, pool));


  if (! opt_state->quiet)
    SVN_ERR(svn_cl__notifier_print_conflict_stats(ctx->notify_baton2, pool));

  return SVN_NO_ERROR;
}
