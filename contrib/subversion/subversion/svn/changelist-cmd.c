/*
 * changelist-cmd.c -- Associate (or deassociate) a wc path with a changelist.
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

#include "svn_client.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_utf.h"

#include "cl.h"

#include "svn_private_config.h"




/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__changelist(apr_getopt_t *os,
                   void *baton,
                   apr_pool_t *pool)
{
  const char *changelist_name = NULL;
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  svn_depth_t depth = opt_state->depth;
  apr_array_header_t *errors = apr_array_make(pool, 0, sizeof(apr_status_t));

  /* If we're not removing changelists, then our first argument should
     be the name of a changelist. */

  if (! opt_state->remove)
    {
      apr_array_header_t *args;
      SVN_ERR(svn_opt_parse_num_args(&args, os, 1, pool));
      changelist_name = APR_ARRAY_IDX(args, 0, const char *);
      SVN_ERR(svn_utf_cstring_to_utf8(&changelist_name,
                                      changelist_name, pool));
    }

  /* Parse the remaining arguments as paths. */
  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* Changelist has no implicit dot-target `.', so don't you put that
     code here! */
  if (! targets->nelts)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  SVN_ERR(svn_cl__check_targets_are_local_paths(targets));

  if (opt_state->quiet)
    ctx->notify_func2 = NULL; /* Easy out: avoid unneeded work */

  if (depth == svn_depth_unknown)
    depth = svn_depth_empty;

  SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, pool));

  if (changelist_name)
    {
      SVN_ERR(svn_cl__try(
               svn_client_add_to_changelist(targets, changelist_name,
                                            depth, opt_state->changelists,
                                            ctx, pool),
               errors, opt_state->quiet,
               SVN_ERR_UNVERSIONED_RESOURCE,
               SVN_ERR_WC_PATH_NOT_FOUND,
               0));
    }
  else
    {
      SVN_ERR(svn_cl__try(
               svn_client_remove_from_changelists(targets, depth,
                                                  opt_state->changelists,
                                                  ctx, pool),
               errors, opt_state->quiet,
               SVN_ERR_UNVERSIONED_RESOURCE,
               SVN_ERR_WC_PATH_NOT_FOUND,
               0));
    }

  if (errors->nelts > 0)
    {
      int i;
      svn_error_t *err;

      err = svn_error_create(SVN_ERR_ILLEGAL_TARGET, NULL, NULL);
      for (i = 0; i < errors->nelts; i++)
        {
          apr_status_t status = APR_ARRAY_IDX(errors, i, apr_status_t);

          if (status == SVN_ERR_WC_PATH_NOT_FOUND)
            err = svn_error_quick_wrap(err,
                                       _("Could not set changelists on "
                                         "all targets because some targets "
                                         "don't exist"));
          else if (status == SVN_ERR_UNVERSIONED_RESOURCE)
            err = svn_error_quick_wrap(err,
                                       _("Could not set changelists on "
                                         "all targets because some targets "
                                         "are not versioned"));
        }

      return svn_error_trace(err);
    }

  return SVN_NO_ERROR;
}
