/*
 * revert-cmd.c -- Subversion revert command
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
#include "svn_error_codes.h"
#include "svn_error.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__revert(apr_getopt_t *os,
               void *baton,
               apr_pool_t *scratch_pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets = NULL;
  svn_error_t *err;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE,
                                                      scratch_pool));

  /* Revert has no implicit dot-target `.', so don't you put that code here! */
  if (! targets->nelts)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  /* Revert is especially conservative, by default it is as
     nonrecursive as possible. */
  if (opt_state->depth == svn_depth_unknown)
    opt_state->depth = svn_depth_empty;

  SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, scratch_pool));

  SVN_ERR(svn_cl__check_targets_are_local_paths(targets));

  err = svn_client_revert3(targets, opt_state->depth,
                           opt_state->changelists,
                           FALSE /* clear_changelists */,
                           FALSE /* metadata_only */,
                           ctx, scratch_pool);
  if (err
      && (err->apr_err == SVN_ERR_WC_INVALID_OPERATION_DEPTH)
      && (! SVN_DEPTH_IS_RECURSIVE(opt_state->depth)))
    {
      err = svn_error_quick_wrap
        (err, _("Try 'svn revert --depth infinity' instead?"));
    }

  return svn_error_trace(err);
}
