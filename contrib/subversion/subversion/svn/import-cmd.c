/*
 * import-cmd.c -- Import a file or tree into the repository.
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
#include "svn_error.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__import(apr_getopt_t *os,
               void *baton,
               apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  const char *path;
  const char *url;

  /* Import takes two arguments, for example
   *
   *   $ svn import projects/test file:///home/jrandom/repos/trunk
   *                ^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
   *                 (source)       (repository)
   *
   * or
   *
   *   $ svn import file:///home/jrandom/repos/some/subdir
   *
   * What is the nicest behavior for import, from the user's point of
   * view?  This is a subtle question.  Seemingly intuitive answers
   * can lead to weird situations, such never being able to create
   * non-directories in the top-level of the repository.
   *
   * If 'source' is a file then the basename of 'url' is used as the
   * filename in the repository.  If 'source' is a directory then the
   * import happens directly in the repository target dir, creating
   * however many new entries are necessary.  If some part of 'url'
   * does not exist in the repository then parent directories are created
   * as necessary.
   *
   * In the case where no 'source' is given '.' (the current directory)
   * is implied.
   *
   * ### kff todo: review above behaviors.
   */

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  if (targets->nelts < 1)
    return svn_error_create
      (SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
       _("Repository URL required when importing"));
  else if (targets->nelts > 2)
    return svn_error_create
      (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
       _("Too many arguments to import command"));
  else if (targets->nelts == 1)
    {
      url = APR_ARRAY_IDX(targets, 0, const char *);
      path = "";
    }
  else
    {
      path = APR_ARRAY_IDX(targets, 0, const char *);
      url = APR_ARRAY_IDX(targets, 1, const char *);
    }

  SVN_ERR(svn_cl__check_target_is_local_path(path));

  if (! svn_path_is_url(url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Invalid URL '%s'"), url);

  if (opt_state->depth == svn_depth_unknown)
    opt_state->depth = svn_depth_infinity;

  SVN_ERR(svn_cl__make_log_msg_baton(&(ctx->log_msg_baton3), opt_state,
                                     NULL, ctx->config, pool));

  SVN_ERR(svn_cl__cleanup_log_msg
          (ctx->log_msg_baton3,
           svn_client_import5(path,
                              url,
                              opt_state->depth,
                              opt_state->no_ignore,
                              opt_state->no_autoprops,
                              opt_state->force,
                              opt_state->revprop_table,
                              NULL, NULL,  /* filter callback / baton */
                              (opt_state->quiet
                               ? NULL : svn_cl__print_commit_info),
                              NULL,
                              ctx,
                              pool), pool));

  return SVN_NO_ERROR;
}
