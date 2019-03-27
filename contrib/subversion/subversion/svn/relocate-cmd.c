/*
 * relocate-cmd.c -- Update working tree administrative data to
 *                   account for repository change-of-address.
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

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__relocate(apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *scratch_pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  svn_boolean_t ignore_externals = opt_state->ignore_externals;
  apr_array_header_t *targets;
  const char *from, *to, *path;

  /* We've got two different syntaxes to support:

     1. relocate FROM-PREFIX TO-PREFIX [PATH ...]
     2. relocate TO-URL [PATH]
  */
  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE,
                                                      scratch_pool));
  if (targets->nelts < 1)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  /* If we have a single target, we're in form #2.  If we have two
     targets and the first is a URL and the second is not, we're also
     in form #2.  */
  if ((targets->nelts == 1) ||
      ((targets->nelts == 2)
       && (svn_path_is_url(APR_ARRAY_IDX(targets, 0, const char *)))
       && (! svn_path_is_url(APR_ARRAY_IDX(targets, 1, const char *)))))

    {
      to = APR_ARRAY_IDX(targets, 0, const char *);
      path = (targets->nelts == 2) ? APR_ARRAY_IDX(targets, 1, const char *)
                                   : "";

      SVN_ERR(svn_client_url_from_path2(&from, path, ctx,
                                        scratch_pool, scratch_pool));
      SVN_ERR(svn_client_relocate2(path, from, to, ignore_externals,
                                   ctx, scratch_pool));
    }
  /* ... Everything else is form #1. */
  else
    {
      from = APR_ARRAY_IDX(targets, 0, const char *);
      to = APR_ARRAY_IDX(targets, 1, const char *);

      if (targets->nelts == 2)
        {
          SVN_ERR(svn_client_relocate2("", from, to, ignore_externals,
                                       ctx, scratch_pool));
        }
      else
        {
          apr_pool_t *subpool = svn_pool_create(scratch_pool);
          int i;

          /* Target working copy root dir must be local. */
          for (i = 2; i < targets->nelts; i++)
            {
              path = APR_ARRAY_IDX(targets, i, const char *);
              SVN_ERR(svn_cl__check_target_is_local_path(path));
            }

          for (i = 2; i < targets->nelts; i++)
            {
              svn_pool_clear(subpool);
              path = APR_ARRAY_IDX(targets, i, const char *);
              SVN_ERR(svn_client_relocate2(path, from, to, ignore_externals,
                                           ctx, subpool));
            }
          svn_pool_destroy(subpool);
        }
    }

  return SVN_NO_ERROR;
}
