/*
 * move-cmd.c -- Subversion move command
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
#include "svn_error.h"
#include "svn_path.h"
#include "cl.h"

#include "svn_private_config.h"



/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__move(apr_getopt_t *os,
             void *baton,
             apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  const char *dst_path;
  svn_error_t *err;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, TRUE, pool));

  if (targets->nelts < 2)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  if (opt_state->start_revision.kind != svn_opt_revision_unspecified
      && opt_state->start_revision.kind != svn_opt_revision_head)
    {
      return svn_error_create
        (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         _("Cannot specify revisions (except HEAD) with move operations"));
    }

  dst_path = APR_ARRAY_IDX(targets, targets->nelts - 1, const char *);
  apr_array_pop(targets);

  if (! svn_path_is_url(dst_path))
    {
      ctx->log_msg_func3 = NULL;
      if (opt_state->message || opt_state->filedata || opt_state->revprop_table)
        return svn_error_create
          (SVN_ERR_CL_UNNECESSARY_LOG_MESSAGE, NULL,
           _("Local, non-commit operations do not take a log message "
             "or revision properties"));
    }

  if (ctx->log_msg_func3)
    SVN_ERR(svn_cl__make_log_msg_baton(&(ctx->log_msg_baton3), opt_state,
                                       NULL, ctx->config, pool));

  SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, pool));

  err = svn_client_move7(targets, dst_path,
                         TRUE /* move_as_child */,
                         opt_state->parents /* make_parents */,
                         opt_state->allow_mixed_rev /* allow_mixed_revisions*/,
                         FALSE /* metadata_only */,
                         opt_state->revprop_table,
                         (opt_state->quiet ? NULL : svn_cl__print_commit_info),
                         NULL, ctx, pool);

  if (err)
    err = svn_cl__may_need_force(err);

  if (ctx->log_msg_func3)
    SVN_ERR(svn_cl__cleanup_log_msg(ctx->log_msg_baton3, err, pool));
  else if (err)
    return svn_error_trace(err);

  return SVN_NO_ERROR;
}
