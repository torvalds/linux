/*
 * list-cmd.c -- list a URL
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

#include "svn_cmdline.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_time.h"
#include "svn_xml.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "svn_opt.h"

#include "cl.h"

#include "svn_private_config.h"
#include "private/svn_string_private.h"



/* Baton used when printing directory entries. */
struct print_baton {
  svn_boolean_t verbose;
  apr_int64_t directories;
  apr_int64_t files;
  apr_int64_t locks;
  svn_client_ctx_t *ctx;
};

/* Field flags required for this function */
static const apr_uint32_t print_dirent_fields = SVN_DIRENT_KIND;
static const apr_uint32_t print_dirent_fields_verbose = (
    SVN_DIRENT_KIND  | SVN_DIRENT_SIZE | SVN_DIRENT_TIME |
    SVN_DIRENT_CREATED_REV | SVN_DIRENT_LAST_AUTHOR);

/* This implements the svn_client_list_func2_t API, printing a single
   directory entry in text format. */
static svn_error_t *
print_dirent(void *baton,
             const char *path,
             const svn_dirent_t *dirent,
             const svn_lock_t *lock,
             const char *abs_path,
             const char *external_parent_url,
             const char *external_target,
             apr_pool_t *pool)
{
  struct print_baton *pb = baton;

  if (pb->ctx->cancel_func)
    SVN_ERR(pb->ctx->cancel_func(pb->ctx->cancel_baton));

  if (dirent->kind == svn_node_dir)
    pb->directories++;
  if (dirent->kind == svn_node_file)
    pb->files++;
  if (lock)
    pb->locks++;

  return SVN_NO_ERROR;
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__null_list(apr_getopt_t *os,
                  void *baton,
                  apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  int i;
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_uint32_t dirent_fields;
  struct print_baton pb = { FALSE };
  svn_boolean_t seen_nonexistent_target = FALSE;
  svn_error_t *err;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* Add "." if user passed 0 arguments */
  svn_opt_push_implicit_dot_target(targets, pool);

  if (opt_state->verbose)
    dirent_fields = print_dirent_fields_verbose;
  else
    dirent_fields = print_dirent_fields;

  pb.ctx = ctx;
  pb.verbose = opt_state->verbose;

  if (opt_state->depth == svn_depth_unknown)
    opt_state->depth = svn_depth_immediates;

  /* For each target, try to list it. */
  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      const char *truepath;
      svn_opt_revision_t peg_revision;
      apr_array_header_t *patterns = NULL;
      int k;

      svn_pool_clear(subpool);

      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      /* Get peg revisions. */
      SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target,
                                 subpool));

      if (opt_state->search_patterns)
        {
          patterns = apr_array_make(subpool, 4, sizeof(const char *));
          for (k = 0; k < opt_state->search_patterns->nelts; ++k)
            {
              apr_array_header_t *pattern_group
                = APR_ARRAY_IDX(opt_state->search_patterns, k,
                                apr_array_header_t *);

              /* Should never fail but ... */
              if (pattern_group->nelts != 1)
                return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                  _("'search-and' option is not supported"));

              APR_ARRAY_PUSH(patterns, const char *)
                = APR_ARRAY_IDX(pattern_group, 0, const char *);
            }
        }

      err = svn_client_list4(truepath, &peg_revision,
                             &(opt_state->start_revision), patterns,
                             opt_state->depth,
                             dirent_fields,
                             opt_state->verbose,
                             FALSE, /* include externals */
                             print_dirent,
                             &pb, ctx, subpool);

      if (err)
        {
          /* If one of the targets is a non-existent URL or wc-entry,
             don't bail out.  Just warn and move on to the next target. */
          if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND ||
              err->apr_err == SVN_ERR_FS_NOT_FOUND)
              svn_handle_warning2(stderr, err, "svnbench: ");
          else
              return svn_error_trace(err);

          svn_error_clear(err);
          err = NULL;
          seen_nonexistent_target = TRUE;
        }
      else if (!opt_state->quiet)
        SVN_ERR(svn_cmdline_printf(pool,
                                   _("%15s directories\n"
                                     "%15s files\n"
                                     "%15s locks\n"),
                                   svn__ui64toa_sep(pb.directories, ',', pool),
                                   svn__ui64toa_sep(pb.files, ',', pool),
                                   svn__ui64toa_sep(pb.locks, ',', pool)));
    }

  svn_pool_destroy(subpool);

  if (seen_nonexistent_target)
    return svn_error_create(
      SVN_ERR_ILLEGAL_TARGET, NULL,
      _("Could not list all targets because some targets don't exist"));
  else
    return SVN_NO_ERROR;
}
