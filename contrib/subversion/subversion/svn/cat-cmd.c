/*
 * cat-cmd.c -- Print the content of a file or URL.
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
#include "svn_error.h"
#include "svn_opt.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__cat(apr_getopt_t *os,
            void *baton,
            apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  int i;
  svn_stream_t *out;
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_array_header_t *errors = apr_array_make(pool, 0, sizeof(apr_status_t));
  svn_error_t *err;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* Cat cannot operate on an implicit '.' so a filename is required */
  if (! targets->nelts)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  SVN_ERR(svn_stream_for_stdout(&out, pool));

  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      const char *truepath;
      svn_opt_revision_t peg_revision;

      svn_pool_clear(subpool);
      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      /* Get peg revisions. */
      SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target,
                                 subpool));

      SVN_ERR(svn_cl__try(svn_client_cat3(NULL, out, truepath, &peg_revision,
                                          &(opt_state->start_revision),
                                          !opt_state->ignore_keywords,
                                          ctx, subpool, subpool),
                           errors, opt_state->quiet,
                           SVN_ERR_UNVERSIONED_RESOURCE,
                           SVN_ERR_ENTRY_NOT_FOUND,
                           SVN_ERR_CLIENT_IS_DIRECTORY,
                           SVN_ERR_FS_NOT_FOUND,
                           0));
    }
  svn_pool_destroy(subpool);

  if (errors->nelts > 0)
    {
      err = svn_error_create(SVN_ERR_ILLEGAL_TARGET, NULL, NULL);

      for (i = 0; i < errors->nelts; i++)
        {
          apr_status_t status = APR_ARRAY_IDX(errors, i, apr_status_t);

          if (status == SVN_ERR_ENTRY_NOT_FOUND ||
              status == SVN_ERR_FS_NOT_FOUND)
            err = svn_error_quick_wrap(err,
                                       _("Could not cat all targets because "
                                         "some targets don't exist"));
          else if (status == SVN_ERR_UNVERSIONED_RESOURCE)
            err = svn_error_quick_wrap(err,
                                       _("Could not cat all targets because "
                                         "some targets are not versioned"));
          else if (status == SVN_ERR_CLIENT_IS_DIRECTORY)
            err = svn_error_quick_wrap(err,
                                       _("Could not cat all targets because "
                                         "some targets are directories"));
        }

      return svn_error_trace(err);
    }

  return SVN_NO_ERROR;
}
