/*
 * info-cmd.c -- Display information about a resource
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

#include "svn_string.h"
#include "svn_cmdline.h"
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_time.h"
#include "svn_xml.h"
#include "cl.h"

#include "svn_private_config.h"
#include "private/svn_client_private.h"


/*** Code. ***/

/* The dirent fields we care about for our calls to svn_ra_get_dir2. */
#define DIRENT_FIELDS (SVN_DIRENT_KIND        | \
                       SVN_DIRENT_CREATED_REV | \
                       SVN_DIRENT_TIME        | \
                       SVN_DIRENT_LAST_AUTHOR)

/* Helper func for recursively fetching svn_dirent_t's from a remote
   directory and pushing them at an info-receiver callback.

   DEPTH is the depth starting at DIR, even though RECEIVER is never
   invoked on DIR: if DEPTH is svn_depth_immediates, then increment
   *COUNTER on all children of DIR, but none of their children; if
   svn_depth_files, then increment *COUNTER on file children of DIR but
   not on subdirectories; if svn_depth_infinity, recurse fully.
   DIR is a relpath, relative to the root of RA_SESSION.
*/
static svn_error_t *
push_dir_info(svn_ra_session_t *ra_session,
              const svn_client__pathrev_t *pathrev,
              const char *dir,
              int *counter,
              svn_depth_t depth,
              svn_client_ctx_t *ctx,
              apr_pool_t *pool)
{
  apr_hash_t *tmpdirents;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(svn_ra_get_dir2(ra_session, &tmpdirents, NULL, NULL,
                          dir, pathrev->rev, DIRENT_FIELDS, pool));

  for (hi = apr_hash_first(pool, tmpdirents); hi; hi = apr_hash_next(hi))
    {
      const char *path;
      const char *name = apr_hash_this_key(hi);
      svn_dirent_t *the_ent = apr_hash_this_val(hi);
      svn_client__pathrev_t *child_pathrev;

      svn_pool_clear(subpool);

      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      path = svn_relpath_join(dir, name, subpool);
      child_pathrev = svn_client__pathrev_join_relpath(pathrev, name, subpool);

      if (depth >= svn_depth_immediates
          || (depth == svn_depth_files && the_ent->kind == svn_node_file))
        ++(*counter);

      if (depth == svn_depth_infinity && the_ent->kind == svn_node_dir)
        SVN_ERR(push_dir_info(ra_session, child_pathrev, path,
                              counter, depth, ctx, subpool));
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* Stripped-down version of svn_client_info3 */
static svn_error_t *
client_info(const char *abspath_or_url,
            const svn_opt_revision_t *peg_revision,
            const svn_opt_revision_t *revision,
            svn_depth_t depth,
            svn_boolean_t fetch_excluded,
            svn_boolean_t fetch_actual_only,
            const apr_array_header_t *changelists,
            int *counter,
            svn_client_ctx_t *ctx,
            apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  svn_client__pathrev_t *pathrev;
  svn_lock_t *lock;
  const char *base_name;
  svn_dirent_t *the_ent;
  svn_error_t *err;

  if (depth == svn_depth_unknown)
    depth = svn_depth_empty;

  /* Go repository digging instead. */

  /* Trace rename history (starting at path_or_url@peg_revision) and
     return RA session to the possibly-renamed URL as it exists in REVISION.
     The ra_session returned will be anchored on this "final" URL. */
  SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &pathrev,
                                            abspath_or_url, NULL, peg_revision,
                                            revision, ctx, pool));

  svn_uri_split(NULL, &base_name, pathrev->url, pool);

  /* Get the dirent for the URL itself. */
  SVN_ERR(svn_ra_stat(ra_session, "", pathrev->rev, &the_ent, pool));

  if (! the_ent)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("URL '%s' non-existent in revision %ld"),
                             pathrev->url, pathrev->rev);

  /* check for locks */
  err = svn_ra_get_lock(ra_session, &lock, "", pool);

  /* An old mod_dav_svn will always work; there's nothing wrong with
      doing a PROPFIND for a property named "DAV:supportedlock". But
      an old svnserve will error. */
  if (err && err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED)
    {
      svn_error_clear(err);
      lock = NULL;
    }
  else if (err)
    return svn_error_trace(err);

  /* Push the URL's dirent (and lock) at the callback.*/
  ++(*counter);

  /* Possibly recurse, using the original RA session. */
  if (depth > svn_depth_empty && (the_ent->kind == svn_node_dir))
    {
      apr_hash_t *locks;

      if (peg_revision->kind == svn_opt_revision_head)
        {
          err = svn_ra_get_locks2(ra_session, &locks, "", depth, pool);

          /* Catch specific errors thrown by old mod_dav_svn or svnserve. */
          if (err &&
              (err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED
               || err->apr_err == SVN_ERR_UNSUPPORTED_FEATURE))
            svn_error_clear(err);
          else if (err)
            return svn_error_trace(err);
        }

      SVN_ERR(push_dir_info(ra_session, pathrev, "",
                            counter, depth, ctx, pool));
    }

  return SVN_NO_ERROR;
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__null_info(apr_getopt_t *os,
                  void *baton,
                  apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets = NULL;
  apr_pool_t *subpool = svn_pool_create(pool);
  int i;
  svn_error_t *err;
  svn_boolean_t seen_nonexistent_target = FALSE;
  svn_opt_revision_t peg_revision;
  const char *path_prefix;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* Add "." if user passed 0 arguments. */
  svn_opt_push_implicit_dot_target(targets, pool);

  if (opt_state->depth == svn_depth_unknown)
    opt_state->depth = svn_depth_empty;

  SVN_ERR(svn_dirent_get_absolute(&path_prefix, "", pool));

  for (i = 0; i < targets->nelts; i++)
    {
      const char *truepath;
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      int received_count = 0;

      svn_pool_clear(subpool);
      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      /* Get peg revisions. */
      SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target, subpool));

      /* If no peg-rev was attached to a URL target, then assume HEAD. */
      if (svn_path_is_url(truepath))
        {
          if (peg_revision.kind == svn_opt_revision_unspecified)
            peg_revision.kind = svn_opt_revision_head;
        }
      else
        {
          SVN_ERR(svn_dirent_get_absolute(&truepath, truepath, subpool));
        }

      err = client_info(truepath,
                        &peg_revision, &(opt_state->start_revision),
                        opt_state->depth, TRUE, TRUE,
                        NULL,
                        &received_count,
                        ctx, subpool);

      if (err)
        {
          /* If one of the targets is a non-existent URL or wc-entry,
             don't bail out.  Just warn and move on to the next target. */
          if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND ||
              err->apr_err == SVN_ERR_RA_ILLEGAL_URL)
            {
              svn_handle_warning2(stderr, err, "svnbench: ");
              svn_error_clear(svn_cmdline_fprintf(stderr, subpool, "\n"));
            }
          else
            {
              return svn_error_trace(err);
            }

          svn_error_clear(err);
          err = NULL;
          seen_nonexistent_target = TRUE;
        }
      else
        {
          SVN_ERR(svn_cmdline_printf(pool, _("Number of status notifications "
                                             "received: %d\n"),
                                     received_count));
        }
    }
  svn_pool_destroy(subpool);

  if (seen_nonexistent_target)
    return svn_error_create(
      SVN_ERR_ILLEGAL_TARGET, NULL,
      _("Could not display info for all targets because some "
        "targets don't exist"));
  else
    return SVN_NO_ERROR;
}
