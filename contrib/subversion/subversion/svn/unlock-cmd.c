/*
 * unlock-cmd.c -- Unlock a working copy path.
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
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_cmdline.h"
#include "cl.h"
#include "svn_private_config.h"


/*** Code. ***/

/* Baton for notify_unlock_handler */
struct notify_unlock_baton_t
{
  void *inner_baton;
  svn_wc_notify_func2_t inner_notify;
  svn_boolean_t had_failure;
};

/* Implements svn_wc_notify_func2_t for svn_cl__unlock */
static void
notify_unlock_handler(void *baton,
                      const svn_wc_notify_t *notify,
                      apr_pool_t *scratch_pool)
{
  struct notify_unlock_baton_t *nub = baton;

  if (notify->action == svn_wc_notify_failed_unlock)
    nub->had_failure = TRUE;

  if (nub->inner_notify)
    nub->inner_notify(nub->inner_baton, notify, scratch_pool);
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__unlock(apr_getopt_t *os,
               void *baton,
               apr_pool_t *scratch_pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  struct notify_unlock_baton_t nub;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE,
                                                      scratch_pool));

  /* We don't support unlock on directories, so "." is not relevant. */
  if (! targets->nelts)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, scratch_pool));

  SVN_ERR(svn_cl__assert_homogeneous_target_type(targets));

  nub.inner_notify = ctx->notify_func2;
  nub.inner_baton = ctx->notify_baton2;
  nub.had_failure = FALSE;

  ctx->notify_func2 = notify_unlock_handler;
  ctx->notify_baton2 = &nub;

  SVN_ERR(svn_client_unlock(targets, opt_state->force, ctx, scratch_pool));

  if (nub.had_failure)
    return svn_error_create(SVN_ERR_ILLEGAL_TARGET, NULL,
                            _("One or more locks could not be released"));

  return SVN_NO_ERROR;
}
