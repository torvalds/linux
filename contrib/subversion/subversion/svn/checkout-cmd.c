/*
 * checkout-cmd.c -- Subversion checkout command
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
#include "svn_pools.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/*
  This is what it does

  - case 1: one URL
    $ svn co http://host/repos/module
    checkout into ./module/

  - case 2: one URL and explicit path
    $ svn co http://host/repos/module path
    checkout into ./path/

  - case 3: multiple URLs
    $ svn co http://host1/repos1/module1 http://host2/repos2/module2
    checkout into ./module1/ and ./module2/

  - case 4: multiple URLs and explicit path
    $ svn co http://host1/repos1/module1 http://host2/repos2/module2 path
    checkout into ./path/module1/ and ./path/module2/

  Is this the same as CVS?  Does it matter if it is not?
*/


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__checkout(apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_pool_t *subpool;
  apr_array_header_t *targets;
  struct svn_cl__check_externals_failed_notify_baton nwb;
  const char *last_target, *local_dir;
  int i;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  if (! targets->nelts)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL, NULL);

  /* Determine LOCAL_DIR (case 1: URL basename; 2,4: specified; 3: "")
   * and leave TARGETS holding just the source URLs. */
  last_target = APR_ARRAY_IDX(targets, targets->nelts - 1, const char *);
  if (svn_path_is_url(last_target))
    {
      if (targets->nelts == 1)
        {
          svn_opt_revision_t pegrev;

          /* Use the URL basename, discarding any peg revision. */
          SVN_ERR(svn_opt_parse_path(&pegrev, &local_dir, last_target, pool));
          local_dir = svn_uri_basename(local_dir, pool);
        }
      else
        {
          local_dir = "";
        }
    }
  else
    {
      if (targets->nelts == 1)
        /* What?  They gave us one target, and it wasn't a URL. */
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL, NULL);

      apr_array_pop(targets);
      local_dir = last_target;
    }

  if (! opt_state->quiet)
    SVN_ERR(svn_cl__notifier_mark_checkout(ctx->notify_baton2));

  nwb.wrapped_func = ctx->notify_func2;
  nwb.wrapped_baton = ctx->notify_baton2;
  nwb.had_externals_error = FALSE;
  ctx->notify_func2 = svn_cl__check_externals_failed_notify_wrapper;
  ctx->notify_baton2 = &nwb;

  subpool = svn_pool_create(pool);
  for (i = 0; i < targets->nelts; ++i)
    {
      const char *repos_url = APR_ARRAY_IDX(targets, i, const char *);
      const char *target_dir;
      const char *true_url;
      svn_opt_revision_t revision = opt_state->start_revision;
      svn_opt_revision_t peg_revision;

      svn_pool_clear(subpool);

      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      /* Validate the REPOS_URL */
      if (! svn_path_is_url(repos_url))
        return svn_error_createf
          (SVN_ERR_BAD_URL, NULL,
           _("'%s' does not appear to be a URL"), repos_url);

      /* Get a possible peg revision. */
      SVN_ERR(svn_opt_parse_path(&peg_revision, &true_url, repos_url,
                                 subpool));

      /* Use sub-directory of destination if checking-out multiple URLs */
      if (targets->nelts == 1)
        {
          target_dir = local_dir;
        }
      else
        {
          target_dir = svn_dirent_join(local_dir,
                                       svn_uri_basename(true_url, subpool),
                                       subpool);
        }

      /* Checkout doesn't accept an unspecified revision, so default to
         the peg revision, or to HEAD if there wasn't a peg. */
      if (revision.kind == svn_opt_revision_unspecified)
      {
        if (peg_revision.kind != svn_opt_revision_unspecified)
          revision = peg_revision;
        else
          revision.kind = svn_opt_revision_head;
      }

      SVN_ERR(svn_client_checkout3
              (NULL, true_url, target_dir,
               &peg_revision,
               &revision,
               opt_state->depth,
               opt_state->ignore_externals,
               opt_state->force,
               ctx, subpool));
    }
  svn_pool_destroy(subpool);

  if (nwb.had_externals_error)
    return svn_error_create(SVN_ERR_CL_ERROR_PROCESSING_EXTERNALS, NULL,
                            _("Failure occurred processing one or "
                              "more externals definitions"));

  return SVN_NO_ERROR;
}
