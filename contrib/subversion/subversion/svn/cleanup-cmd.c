/*
 * cleanup-cmd.c -- Subversion cleanup command
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
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__cleanup(apr_getopt_t *os,
                void *baton,
                apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  apr_pool_t *iterpool;
  int i;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* Add "." if user passed 0 arguments */
  svn_opt_push_implicit_dot_target(targets, pool);

  SVN_ERR(svn_cl__check_targets_are_local_paths(targets));

  SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, pool));

  iterpool = svn_pool_create(pool);
  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      const char *target_abspath;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      SVN_ERR(svn_dirent_get_absolute(&target_abspath, target, iterpool));

      if (opt_state->remove_unversioned || opt_state->remove_ignored ||
          opt_state->vacuum_pristines)
        {
          svn_error_t *err = svn_client_vacuum(target_abspath,
                                               opt_state->remove_unversioned,
                                               opt_state->remove_ignored,
                                               TRUE /* fix_timestamps */,
                                               opt_state->vacuum_pristines,
                                               opt_state->include_externals,
                                               ctx, iterpool);

          if (err && err->apr_err == SVN_ERR_WC_LOCKED)
            err = svn_error_create(SVN_ERR_WC_LOCKED, err,
                                     _("Working copy locked; if no other "
                                       "Subversion client is currently "
                                       "using the working copy, try running "
                                       "'svn cleanup' without the "
                                       "--remove-unversioned and "
                                       "--remove-ignored options first."));
          else if (err && err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY)
            err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, err,
                                     _("Cannot remove unversioned or ignored "
                                       "items from something that is not a "
                                       "working copy"));

          SVN_ERR(err);
        }
      else
        {
          svn_error_t *err = svn_client_cleanup2(target_abspath,
                                                 TRUE /* break_locks */,
                                                 TRUE /* fix_timestamps */,
                                                 TRUE /* clear_dav_cache */,
                                                 TRUE /* vacuum_pristines */,
                                                 opt_state->include_externals,
                                                 ctx, iterpool);

          if (err && err->apr_err == SVN_ERR_WC_LOCKED)
            {
              const char *wcroot_abspath;
              svn_error_t *err2;

              err2 = svn_client_get_wc_root(&wcroot_abspath, target_abspath,
                                            ctx, iterpool, iterpool);
              if (err2)
                err =  svn_error_compose_create(err, err2);
              else
                err = svn_error_createf(SVN_ERR_WC_LOCKED, err,
                                        _("Working copy locked; try running "
                                          "'svn cleanup' on the root of the "
                                          "working copy ('%s') instead."),
                                          svn_dirent_local_style(wcroot_abspath,
                                                                 iterpool));
            }
          SVN_ERR(err);
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}
